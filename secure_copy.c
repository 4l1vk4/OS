#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdint.h>
#include "libcaesar.h"
#include "librc4.h"

#define BUFFER_SIZE 4096
#define CAESAR_WORKERS_COUNT 4
#define CONTAINER_WORKERS_COUNT 5
#define MAX_DEPTH 6

#define MODE_AUTO 0
#define MODE_SEQUENTIAL 1
#define MODE_PARALLEL 2

volatile int keep_running = 1;

typedef struct {
    char **filenames;       
    int total_files;        
    int current_index;      
    char *out_dir;          
    pthread_mutex_t mutex;  
} caesar_pool_t;

typedef struct {
    uint32_t file_size;
    uint32_t name_size;
    unsigned char salt[16];
} container_header_t;

typedef struct {
    char **filenames;
    int total_files;
    int current_index;
    const char *container_path;
    const char *master_key;
    pthread_mutex_t task_mutex;
    pthread_mutex_t write_mutex;
} container_pool_t;

typedef struct {
    char **files;
    int count;
    int capacity;
} file_list_t;

typedef struct {
    char *name;
    uint32_t size;
} list_entry_t;

void handle_sigint(int sig) {
    (void)sig; 
    keep_running = 0;
}

const char* get_basename(const char* path) {
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

void add_to_list(file_list_t *list, const char *path) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        list->files = realloc(list->files, list->capacity * sizeof(char*));
    }
    list->files[list->count++] = strdup(path);
}

void traverse_dir(const char *path, int depth, file_list_t *list) {
    if (depth > MAX_DEPTH || !keep_running) return;

    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && keep_running) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) traverse_dir(full_path, depth + 1, list);
            else if (S_ISREG(st.st_mode)) add_to_list(list, full_path);
        }
    }
    closedir(dir);
}

void generate_salt(unsigned char *salt, size_t length) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        fread(salt, 1, length, f);
        fclose(f);
    }
}

void process_container_file(const char *src_file, const char *container_path, const char *master_key, pthread_mutex_t *write_mutex) {
    if (!keep_running) return;
    FILE *in = fopen(src_file, "rb");
    if (!in) return;
    fseek(in, 0, SEEK_END);
    uint32_t file_size = ftell(in);
    rewind(in);

    uint32_t name_size = strlen(src_file);
    container_header_t header;
    header.file_size = file_size;
    header.name_size = name_size;
    generate_salt(header.salt, 16);

    size_t key_len = strlen(master_key);
    unsigned char *derived_key = malloc(key_len + 16);
    if (!derived_key) { fclose(in); return; }
    memcpy(derived_key, master_key, key_len);
    memcpy(derived_key + key_len, header.salt, 16);

    unsigned char *encrypted_data = malloc(file_size + 1);
    if (!encrypted_data && file_size > 0) { free(derived_key); fclose(in); return; }

    if (file_size > 0) fread(encrypted_data, 1, file_size, in);
    fclose(in);

    rc4_ctx_t *rc4 = rc4_init(derived_key, key_len + 16);
    if (file_size > 0 && rc4) rc4_crypt(rc4, encrypted_data, file_size);
    rc4_cleanup(rc4);
    free(derived_key);

    pthread_mutex_lock(write_mutex);
    FILE *out = fopen(container_path, "ab");
    if (out) {
        fwrite(&header.file_size, sizeof(uint32_t), 1, out);
        fwrite(&header.name_size, sizeof(uint32_t), 1, out);
        fwrite(header.salt, 1, 16, out);
        fwrite(src_file, 1, name_size, out);
        if (file_size > 0) fwrite(encrypted_data, 1, file_size, out);
        fclose(out);
    }
    pthread_mutex_unlock(write_mutex);
    if (encrypted_data) free(encrypted_data);
}

void* container_worker_thread(void* arg) {
    container_pool_t *ctx = (container_pool_t*)arg;
    while (keep_running) {
        pthread_mutex_lock(&ctx->task_mutex);
        if (ctx->current_index >= ctx->total_files) {
            pthread_mutex_unlock(&ctx->task_mutex);
            break;
        }
        int my_index = ctx->current_index++;
        char *src_path = ctx->filenames[my_index];
        pthread_mutex_unlock(&ctx->task_mutex);
        process_container_file(src_path, ctx->container_path, ctx->master_key, &ctx->write_mutex);
    }
    return NULL;
}

void cmd_add(const char *container, const char *master_key, char **files, int num_files) {
    container_pool_t ctx = { files, num_files, 0, container, master_key };
    pthread_mutex_init(&ctx.task_mutex, NULL);
    pthread_mutex_init(&ctx.write_mutex, NULL);

    pthread_t workers[CONTAINER_WORKERS_COUNT];
    int active_threads = (num_files < CONTAINER_WORKERS_COUNT) ? num_files : CONTAINER_WORKERS_COUNT;
    for (int i = 0; i < active_threads; i++) pthread_create(&workers[i], NULL, container_worker_thread, &ctx);
    for (int i = 0; i < active_threads; i++) pthread_join(workers[i], NULL);
    
    pthread_mutex_destroy(&ctx.task_mutex);
    pthread_mutex_destroy(&ctx.write_mutex);
}

int compare_entries(const void *a, const void *b) {
    return strcmp(((list_entry_t*)a)->name, ((list_entry_t*)b)->name);
}

void cmd_list(const char *container) {
    FILE *f = fopen(container, "rb");
    if (!f) return;

    list_entry_t *entries = NULL;
    int count = 0;
    int capacity = 0;
    container_header_t header;

    while (fread(&header.file_size, sizeof(uint32_t), 1, f) == 1) {
        if (fread(&header.name_size, sizeof(uint32_t), 1, f) != 1) break;
        if (fread(header.salt, 1, 16, f) != 16) break;

        char *name = malloc(header.name_size + 1);
        if (!name) break;
        if (fread(name, 1, header.name_size, f) != header.name_size) { free(name); break; }
        name[header.name_size] = '\0';

        if (count >= capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            entries = realloc(entries, capacity * sizeof(list_entry_t));
        }
        entries[count].name = name;
        entries[count].size = header.file_size;
        count++;
        fseek(f, header.file_size, SEEK_CUR);
    }
    fclose(f);

    qsort(entries, count, sizeof(list_entry_t), compare_entries);
    for (int i = 0; i < count; i++) {
        printf("%s (Размер: %u байт)\n", entries[i].name, entries[i].size);
        free(entries[i].name);
    }
    free(entries);
}

void cmd_get(const char *container, const char *target_file, const char *master_key, const char *out_file) {
    FILE *f = fopen(container, "rb");
    if (!f) return;

    container_header_t header;
    while (fread(&header.file_size, sizeof(uint32_t), 1, f) == 1) {
        if (fread(&header.name_size, sizeof(uint32_t), 1, f) != 1) break;
        if (fread(header.salt, 1, 16, f) != 16) break;

        char *name = malloc(header.name_size + 1);
        if (!name) break;
        if (fread(name, 1, header.name_size, f) != header.name_size) { free(name); break; }
        name[header.name_size] = '\0';

        if (strcmp(name, target_file) == 0) {
            FILE *out = fopen(out_file, "wb");
            if (out) {
                size_t key_len = strlen(master_key);
                unsigned char *derived_key = malloc(key_len + 16);
                memcpy(derived_key, master_key, key_len);
                memcpy(derived_key + key_len, header.salt, 16);

                rc4_ctx_t *rc4 = rc4_init(derived_key, key_len + 16);
                free(derived_key);

                unsigned char buffer[BUFFER_SIZE];
                uint32_t remaining = header.file_size;
                while (remaining > 0) {
                    size_t to_read = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
                    size_t bytes_read = fread(buffer, 1, to_read, f);
                    if (bytes_read == 0) break;
                    
                    if (rc4) rc4_crypt(rc4, buffer, bytes_read);
                    fwrite(buffer, 1, bytes_read, out);
                    remaining -= bytes_read;
                }
                rc4_cleanup(rc4);
                fclose(out);
            }
            free(name);
            break;
        }
        free(name);
        fseek(f, header.file_size, SEEK_CUR);
    }
    fclose(f);
}

int caesar_process_file(const char *src_path, const char *out_dir) {
    if (!keep_running) return 0;
    char dest_path[2048];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", out_dir, get_basename(src_path));

    FILE *in = fopen(src_path, "rb");
    if (!in) { fprintf(stderr, "Ошибка чтения: %s\n", src_path); return 0; }
    FILE *out = fopen(dest_path, "wb");
    if (!out) { fprintf(stderr, "Ошибка записи: %s\n", dest_path); fclose(in); return 0; }

    unsigned char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, in)) > 0 && keep_running) {
        caesar(buffer, buffer, bytes_read);
        fwrite(buffer, 1, bytes_read, out);
    }
    fclose(in);
    fclose(out);
    return 1;
}

void* caesar_worker_thread(void* arg) {
    caesar_pool_t *ctx = (caesar_pool_t*)arg;
    while (keep_running) {
        pthread_mutex_lock(&ctx->mutex);
        if (ctx->current_index >= ctx->total_files) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }
        int my_index = ctx->current_index++;
        char *src_path = ctx->filenames[my_index];
        pthread_mutex_unlock(&ctx->mutex);
        caesar_process_file(src_path, ctx->out_dir);
    }
    return NULL;
}

double run_caesar_mode(int mode, char **files, int num_files, const char *out_dir) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (mode == MODE_SEQUENTIAL) {
        for (int i = 0; i < num_files && keep_running; i++) caesar_process_file(files[i], out_dir);
    } else if (mode == MODE_PARALLEL) {
        caesar_pool_t ctx = { files, num_files, 0, (char*)out_dir };
        pthread_mutex_init(&ctx.mutex, NULL);
        
        pthread_t workers[CAESAR_WORKERS_COUNT];
        int active_threads = (num_files < CAESAR_WORKERS_COUNT) ? num_files : CAESAR_WORKERS_COUNT;
        for (int i = 0; i < active_threads; i++) pthread_create(&workers[i], NULL, caesar_worker_thread, &ctx);
        for (int i = 0; i < active_threads; i++) pthread_join(workers[i], NULL);
        pthread_mutex_destroy(&ctx.mutex);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);
    if (argc < 2) {
        printf("Использование:\n");
        printf("  %s [--mode=sequential|parallel|--test-segfault] <file1> [file2...] <out_dir> <key>\n", argv[0]);
        printf("  %s -add|-list|-get [-key <key>] [-image <img.img>] [-out <file>] [files...]\n", argv[0]);
        return 1;
    }

    int is_container_mode = 0;
    const char *cmd = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-add") == 0 || strcmp(argv[i], "-list") == 0 || strcmp(argv[i], "-get") == 0) {
            is_container_mode = 1;
            cmd = argv[i];
            break;
        }
    }

    if (is_container_mode) {
        const char *key = NULL;
        const char *image = NULL;
        const char *out = NULL;
        const char *get_target = NULL;
        char **files = malloc(argc * sizeof(char*));
        int file_count = 0;

        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-add") == 0 || strcmp(argv[i], "-list") == 0 || strcmp(argv[i], "-get") == 0) {
                continue;
            } else if (strcmp(argv[i], "-key") == 0 && i + 1 < argc) {
                key = argv[++i];
            } else if (strcmp(argv[i], "-image") == 0 && i + 1 < argc) {
                image = argv[++i];
            } else if (strcmp(argv[i], "-out") == 0 && i + 1 < argc) {
                out = argv[++i];
            } else {
                if (strcmp(cmd, "-get") == 0 && get_target == NULL) {
                    get_target = argv[i];
                } else {
                    files[file_count++] = argv[i];
                }
            }
        }

        if (strcmp(cmd, "-add") == 0 && image && key && file_count > 0) {
            file_list_t list = {NULL, 0, 0};
            for (int i = 0; i < file_count; i++) {
                struct stat st;
                if (stat(files[i], &st) == 0) {
                    if (S_ISDIR(st.st_mode)) traverse_dir(files[i], 1, &list);
                    else if (S_ISREG(st.st_mode)) add_to_list(&list, files[i]);
                }
            }
            if (list.count > 0) {
                cmd_add(image, key, list.files, list.count);
                for (int i = 0; i < list.count; i++) free(list.files[i]);
                free(list.files);
            }
        } else if (strcmp(cmd, "-list") == 0 && image) {
            cmd_list(image);
        } else if (strcmp(cmd, "-get") == 0 && image && key && out && get_target) {
            cmd_get(image, get_target, key, out);
        }
        
        free(files);
        return 0;
    }

    int mode = MODE_AUTO;
    int first_file_idx = 1;
    int test_attack = 0;

    if (strncmp(argv[1], "--", 2) == 0) {
        if (strcmp(argv[1], "--mode=sequential") == 0) mode = MODE_SEQUENTIAL;
        else if (strcmp(argv[1], "--mode=parallel") == 0) mode = MODE_PARALLEL;
        else if (strcmp(argv[1], "--test-segfault") == 0) test_attack = 1;
        first_file_idx = 2;
    }

    int total_args_for_files = argc - first_file_idx - 2;
    if (total_args_for_files <= 0 && !test_attack) {
        printf("Ошибка: не указаны входные файлы.\n");
        return 1;
    }

    char *out_dir = argv[argc - 2];
    unsigned char key = (unsigned char)atoi(argv[argc - 1]);
    caesar_key(key);

    if (test_attack) {
        trigger_security_violation();
        return 0;
    }

    struct stat st = {0};
    if (stat(out_dir, &st) == -1) mkdir(out_dir, 0777);

    int num_files = total_args_for_files;

    if (mode == MODE_AUTO) {
        printf("--- Режим автоматического выбора ---\n");
        printf("Количество файлов: %d\n", num_files);
        int optimal_mode = (num_files < 5) ? MODE_SEQUENTIAL : MODE_PARALLEL;
        printf("Выбран %s режим.\n\n", optimal_mode == MODE_SEQUENTIAL ? "последовательный" : "параллельный");
        
        double seq_time = run_caesar_mode(MODE_SEQUENTIAL, &argv[first_file_idx], num_files, out_dir);
        
        if (keep_running) {
            double par_time = run_caesar_mode(MODE_PARALLEL, &argv[first_file_idx], num_files, out_dir);

            printf("\n--- Сравнение режимов ---\n");
            printf("\n");
            printf("| %-18s      | %-16s | %-16s|\n", "Режим", "Общее время (мс)", "Ср. время/файл(мс)");
            printf("|--------------------|------------------|-------------------|\n");
            printf("| %-18s | %-16.2f | %-16.2f  |\n", "Sequential", seq_time, seq_time / num_files);
            printf("| %-18s | %-16.2f | %-16.2f  |\n", "Parallel (4 thr)", par_time, par_time / num_files);
            
            if (par_time < seq_time) {
                printf("\nВывод: Параллельный режим быстрее на %.2f мс.\n", seq_time - par_time);
            } else {
                printf("\nВывод: Последовательный режим быстрее на %.2f мс.\n", par_time - seq_time);
            }
        }
    } else {
        printf("Запуск в %s режиме...\n", mode == MODE_SEQUENTIAL ? "ПОСЛЕДОВАТЕЛЬНОМ" : "ПАРАЛЛЕЛЬНОМ");
        double total_time = run_caesar_mode(mode, &argv[first_file_idx], num_files, out_dir);
        
        if (keep_running) {
            printf("\n--- Статистика ---\n");
            printf("Обработано файлов: %d\n", num_files);
            printf("Общее время:       %.2f мс\n", total_time);
            printf("Ср. время на файл: %.2f мс\n", total_time / num_files);
        }
    }

    if (!keep_running) printf("\nОперация прервана\n");
    return 0;
}
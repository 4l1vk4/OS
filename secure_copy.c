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
#include "libcaesar.h"

#define BUFFER_SIZE 4096
#define WORKERS_COUNT 4


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
} thread_pool_t;


void handle_sigint(int sig) {
    (void)sig; 
    keep_running = 0;
}


const char* get_basename(const char* path) {
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}


int process_single_file(const char *src_path, const char *out_dir) {
    if (!keep_running) return 0;

    char dest_path[2048];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", out_dir, get_basename(src_path));

    FILE *in = fopen(src_path, "rb");
    if (!in) {
        fprintf(stderr, "Ошибка чтения: %s\n", src_path);
        return 0;
    }

    FILE *out = fopen(dest_path, "wb");
    if (!out) {
        fprintf(stderr, "Ошибка записи: %s\n", dest_path);
        fclose(in);
        return 0;
    }

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

void* worker_thread(void* arg) {
    thread_pool_t *ctx = (thread_pool_t*)arg;

    while (keep_running) {
        pthread_mutex_lock(&ctx->mutex);
        
        if (ctx->current_index >= ctx->total_files) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }

        int my_index = ctx->current_index++;
        char *src_path = ctx->filenames[my_index];
        
        pthread_mutex_unlock(&ctx->mutex);

        process_single_file(src_path, ctx->out_dir);
    }
    
    return NULL;
}


double run_mode(int mode, char **files, int num_files, const char *out_dir) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (mode == MODE_SEQUENTIAL) {
        for (int i = 0; i < num_files && keep_running; i++) {
            process_single_file(files[i], out_dir);
        }
    } else if (mode == MODE_PARALLEL) {
        thread_pool_t ctx = { files, num_files, 0, (char*)out_dir };
        pthread_mutex_init(&ctx.mutex, NULL);
        
        pthread_t workers[WORKERS_COUNT];
        int active_threads = (num_files < WORKERS_COUNT) ? num_files : WORKERS_COUNT;

        for (int i = 0; i < active_threads; i++) {
            pthread_create(&workers[i], NULL, worker_thread, &ctx);
        }

        for (int i = 0; i < active_threads; i++) {
            pthread_join(workers[i], NULL);
        }
        
        pthread_mutex_destroy(&ctx.mutex);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Использование: %s [--mode=sequential|parallel] <file1> [file2...] <out_dir> <key>\n", argv[0]);
        return 1;
    }

    signal(SIGINT, handle_sigint);

    int mode = MODE_AUTO;
    int first_file_idx = 1;

    if (strncmp(argv[1], "--mode=", 7) == 0) {
        if (strcmp(argv[1], "--mode=sequential") == 0) mode = MODE_SEQUENTIAL;
        else if (strcmp(argv[1], "--mode=parallel") == 0) mode = MODE_PARALLEL;
        first_file_idx = 2;
    }

    int num_files = argc - first_file_idx - 2;
    if (num_files <= 0) {
        printf("Ошибка: не указаны входные файлы.\n");
        return 1;
    }

    char *out_dir = argv[argc - 2];
    unsigned char key = (unsigned char)atoi(argv[argc - 1]);
    caesar_key(key);

    struct stat st = {0};
    if (stat(out_dir, &st) == -1) {
        mkdir(out_dir, 0777);
    }

    if (mode == MODE_AUTO) {
        printf("--- Режим автоматического выбора ---\n");
        printf("Количество файлов: %d\n", num_files);
        int optimal_mode = (num_files < 5) ? MODE_SEQUENTIAL : MODE_PARALLEL;
        printf("Выбран %s режим.\n\n", optimal_mode == MODE_SEQUENTIAL ? "последовательный" : "параллельный");

        double seq_time = run_mode(MODE_SEQUENTIAL, &argv[first_file_idx], num_files, out_dir);
        
        if (keep_running) {
            double par_time = run_mode(MODE_PARALLEL, &argv[first_file_idx], num_files, out_dir);

            printf("\n--- Сравнение режимов ---\n");
            printf("\n");
            printf("| %-18s      | %-16s | %-16s|\n", "Режим", "Общее время (мс)", "Ср. время/файл(vc)");
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
        double total_time = run_mode(mode, &argv[first_file_idx], num_files, out_dir);
        
        if (keep_running) {
            printf("\n--- Статистика ---\n");
            printf("Обработано файлов: %d\n", num_files);
            printf("Общее время:       %.2f мс\n", total_time);
            printf("Ср. время на файл: %.2f мс\n", total_time / num_files);
        }
    }

    if (!keep_running) {
        printf("\nОперация прервана пользователем!\n");
    }

    return 0;
}
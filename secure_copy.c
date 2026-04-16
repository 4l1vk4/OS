#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "libcaesar.h"

#ifdef __APPLE__
int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abs_timeout) {
    struct timespec current_time;
    struct timespec sleep_time = {0, 5000000};

    while (1) {
        int result = pthread_mutex_trylock(mutex);
        if (result == 0) {
            return 0;
        }
        if (result != EBUSY) {
            return result;
        }

        clock_gettime(CLOCK_REALTIME, &current_time);
        if (current_time.tv_sec > abs_timeout->tv_sec ||
           (current_time.tv_sec == abs_timeout->tv_sec && current_time.tv_nsec >= abs_timeout->tv_nsec)) {
            return ETIMEDOUT;
        }

        
        nanosleep(&sleep_time, NULL);
    }
}
#endif

#define BUFFER_SIZE 4096
#define NUM_THREADS 3

volatile int keep_running = 1;

typedef struct {
    char **filenames;       
    int total_files;        
    int current_index;      
    
    int copied_files_count; 
    
    char *out_dir;          
    FILE *log_file;         
    
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

void* worker_thread(void* arg) {
    thread_pool_t *ctx = (thread_pool_t*)arg;
    unsigned char buffer[BUFFER_SIZE];
    unsigned long tid = (unsigned long)pthread_self();

    while (keep_running) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5;

        int lock_err = pthread_mutex_timedlock(&ctx->mutex, &ts);
        if (lock_err != 0) {
            if (lock_err == ETIMEDOUT) {
                fprintf(stderr, "Возможная взаимоблокировка: поток %lu ожидает мьютекс более 5 секунд\n", tid);
                exit(0);
            }
            continue;
        }

        if (ctx->current_index >= ctx->total_files) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }

        int my_index = ctx->current_index++;
        char *src_path = ctx->filenames[my_index];
        pthread_mutex_unlock(&ctx->mutex);

        char dest_path[2048];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", ctx->out_dir, get_basename(src_path));

        struct timeval start, end;
        gettimeofday(&start, NULL);

        int success = 1;
        FILE *in = fopen(src_path, "rb");
        FILE *out = NULL;
        
        if (!in) {
            success = 0;
        } else {
            out = fopen(dest_path, "wb");
            if (!out) {
                success = 0;
                fclose(in);
            }
        }

        if (success) {
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, in)) > 0 && keep_running) {
                caesar(buffer, buffer, bytes_read);
                if (fwrite(buffer, 1, bytes_read, out) != bytes_read) {
                    success = 0;
                    break;
                }
            }
            fclose(in);
            fclose(out);
        }

        if (!keep_running) break;

        gettimeofday(&end, NULL);
        double elapsed_time = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        time_t rawtime;
        struct tm *timeinfo;
        char time_str[80];
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5;
        lock_err = pthread_mutex_timedlock(&ctx->mutex, &ts);
        
        if (lock_err == 0) {
            if (success) {
                ctx->copied_files_count++;
            }
            
            fprintf(ctx->log_file, "[%s] Thread ID: %lu | Файл: %s | Результат: %s | Время: %.3f сек\n", 
                    time_str, tid, src_path, success ? "Успех" : "Ошибка", elapsed_time);
            fflush(ctx->log_file);
            
            pthread_mutex_unlock(&ctx->mutex);
        } else if (lock_err == ETIMEDOUT) {
            fprintf(stderr, "Возможная взаимоблокировка: поток %lu ожидает мьютекс записи лога более 5 секунд\n", tid);
        }
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Использование: %s <file1> [file2 ...] <output_dir> <key>\n", argv[0]);
        return 1;
    }

    signal(SIGINT, handle_sigint);

    int num_files = argc - 3;
    char *out_dir = argv[argc - 2];
    unsigned char key = (unsigned char)atoi(argv[argc - 1]);
    
    caesar_key(key);

    struct stat st = {0};
    if (stat(out_dir, &st) == -1) {
        if (mkdir(out_dir, 0777) == -1) {
            perror("Ошибка создания выходной директории");
            return 1;
        }
    }

    FILE *log_file = fopen("log.txt", "a");
    if (!log_file) {
        perror("Ошибка открытия лога");
        return 1;
    }

    thread_pool_t ctx;
    ctx.filenames = &argv[1];
    ctx.total_files = num_files;
    ctx.current_index = 0;
    ctx.copied_files_count = 0;
    ctx.out_dir = out_dir;
    ctx.log_file = log_file;
    
    pthread_mutex_init(&ctx.mutex, NULL);

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, worker_thread, &ctx);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&ctx.mutex);
    fclose(log_file);

    if (keep_running) {
        printf("Файлы: %d из %d\n", ctx.copied_files_count, ctx.total_files);
    } else {
        printf("\nОперация прервана. Успешно скопировано файлов: %d\n", ctx.copied_files_count);
    }
    
    return 0;
}
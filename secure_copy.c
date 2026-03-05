#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "libcaesar.h"

#define BUFFER_SIZE 4096

volatile int keep_running = 1;

typedef struct {
    unsigned char data[BUFFER_SIZE];
    int bytes_in_buffer;
    int is_eof; 
    
    pthread_mutex_t mutex;
    pthread_cond_t cond_full; 
    pthread_cond_t cond_empty; 
    
    FILE *in;
    FILE *out;
    long total_size;
    long processed_bytes;
} thread_data_t;

void handle_sigint(int sig) {
    (void)sig; 
    keep_running = 0;
}

void* producer_thread(void* arg) {
    thread_data_t *ctx = (thread_data_t*)arg;
    
    while (keep_running) {
        pthread_mutex_lock(&ctx->mutex);
        
        while (ctx->bytes_in_buffer > 0 && keep_running) {
            pthread_cond_wait(&ctx->cond_empty, &ctx->mutex);
        }

        if (!keep_running) {
            pthread_cond_broadcast(&ctx->cond_full); 
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }

        int read_bytes = fread(ctx->data, 1, BUFFER_SIZE, ctx->in);
        if (read_bytes > 0) {
            caesar(ctx->data, ctx->data, read_bytes);
            ctx->bytes_in_buffer = read_bytes;
            ctx->processed_bytes += read_bytes;
        } else {
            ctx->is_eof = 1;
        }

        pthread_cond_signal(&ctx->cond_full); 
        pthread_mutex_unlock(&ctx->mutex);

        if (ctx->is_eof) break;
    }
    return NULL;
}

void* consumer_thread(void* arg) {
    thread_data_t *ctx = (thread_data_t*)arg;
    int last_percent = -1;
    struct timeval last_time = {0, 0};

    while (keep_running) {
        pthread_mutex_lock(&ctx->mutex);

        while (ctx->bytes_in_buffer == 0 && !ctx->is_eof && keep_running) {
            pthread_cond_wait(&ctx->cond_full, &ctx->mutex);
        }

        if (!keep_running && ctx->bytes_in_buffer == 0) {
            pthread_cond_broadcast(&ctx->cond_empty); 
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }

        if (ctx->bytes_in_buffer > 0) {
            fwrite(ctx->data, 1, ctx->bytes_in_buffer, ctx->out);
            ctx->bytes_in_buffer = 0;
        }

        int percent = (ctx->total_size > 0) ? (int)((ctx->processed_bytes * 100) / ctx->total_size) : 100;
        
        struct timeval now;
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - last_time.tv_sec) * 1000 + (now.tv_usec - last_time.tv_usec) / 1000;

        if (percent % 10 == 0 && percent != last_percent && elapsed_ms >= 100) {
            printf("\rProgress: [");
            for(int i = 0; i < percent / 10; i++) printf("=");
            for(int i = percent / 10; i < 10; i++) printf(" ");
            printf("] %d%%", percent);
            fflush(stdout);
            
            last_percent = percent;
            last_time = now;
        }

        int finish = ctx->is_eof;
        pthread_cond_signal(&ctx->cond_empty); 
        pthread_mutex_unlock(&ctx->mutex);

        if (finish) break;
    }
    
    if (ctx->is_eof && keep_running) {
        printf("\rProgress: [==========] 100%%\n");
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <source> <dest> <key>\n", argv[0]);
        return 1;
    }

    signal(SIGINT, handle_sigint);

    caesar_key((unsigned char)atoi(argv[3]));

    FILE *in = fopen(argv[1], "rb");
    if (!in) { 
        perror("Source file error"); 
        return 1; 
    }
    
    FILE *out = fopen(argv[2], "wb");
    if (!out) { 
        perror("Dest file error"); 
        fclose(in);
        return 1; 
    }

    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    rewind(in);

    thread_data_t ctx = { 
        .in = in, 
        .out = out, 
        .total_size = size, 
        .processed_bytes = 0, 
        .bytes_in_buffer = 0, 
        .is_eof = 0 
    };
    
    pthread_mutex_init(&ctx.mutex, NULL);
    pthread_cond_init(&ctx.cond_full, NULL);
    pthread_cond_init(&ctx.cond_empty, NULL);

    pthread_t prod, cons;
    pthread_create(&prod, NULL, producer_thread, &ctx);
    pthread_create(&cons, NULL, consumer_thread, &ctx);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    fclose(in);
    fclose(out);
    pthread_mutex_destroy(&ctx.mutex);
    pthread_cond_destroy(&ctx.cond_full);
    pthread_cond_destroy(&ctx.cond_empty);

    if (!keep_running) {
        printf("\nОперация прервана пользователем\n");
    } else {
        printf("Готово!\n");
    }
    return 0;
}
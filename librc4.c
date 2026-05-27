#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include "librc4.h"

static int handler_set = 0;

static void rc4_memory_violation_handler(int sig, siginfo_t *si, void *unused) {
    (void)unused;
    (void)si;
    fprintf(stderr, "\n[%s] Security violation in RC4 state memory!\n", sig == SIGBUS ? "SIGBUS" : "SIGSEGV");
    exit(1);
}

rc4_ctx_t* rc4_init(const unsigned char *key, int key_len) {
    rc4_ctx_t *ctx = malloc(sizeof(rc4_ctx_t));
    if (!ctx) return NULL;
    
    ctx->page_size = sysconf(_SC_PAGESIZE);
    ctx->state = mmap(NULL, ctx->page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (ctx->state == MAP_FAILED) {
        free(ctx);
        return NULL;
    }

    if (!handler_set) {
        struct sigaction sa;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sa.sa_sigaction = rc4_memory_violation_handler;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        handler_set = 1;
    }

    int i, j = 0;
    unsigned char temp;
    for (i = 0; i < 256; i++) {
        ctx->state[i] = i;
    }

    for (i = 0; i < 256; i++) {
        j = (j + ctx->state[i] + key[i % key_len]) % 256;
        temp = ctx->state[i];
        ctx->state[i] = ctx->state[j];
        ctx->state[j] = temp;
    }

    ctx->i = 0;
    ctx->j = 0;

    mprotect(ctx->state, ctx->page_size, PROT_READ);
    return ctx;
}

void rc4_crypt(rc4_ctx_t *ctx, unsigned char *data, int data_len) {
    if (!ctx || !ctx->state || !data || data_len <= 0) return;
    
    mprotect(ctx->state, ctx->page_size, PROT_READ | PROT_WRITE);

    int k;
    unsigned char temp, t;
    for (k = 0; k < data_len; k++) {
        ctx->i = (ctx->i + 1) % 256;
        ctx->j = (ctx->j + ctx->state[ctx->i]) % 256;

        temp = ctx->state[ctx->i];
        ctx->state[ctx->i] = ctx->state[ctx->j];
        ctx->state[ctx->j] = temp;

        t = (ctx->state[ctx->i] + ctx->state[ctx->j]) % 256;
        data[k] ^= ctx->state[t];
    }

    mprotect(ctx->state, ctx->page_size, PROT_READ);
}

void rc4_cleanup(rc4_ctx_t *ctx) {
    if (ctx && ctx->state && ctx->state != MAP_FAILED) {
        mprotect(ctx->state, ctx->page_size, PROT_READ | PROT_WRITE);
        memset(ctx->state, 0, ctx->page_size);
        mprotect(ctx->state, ctx->page_size, PROT_READ);
        munmap(ctx->state, ctx->page_size);
        free(ctx);
    }
}
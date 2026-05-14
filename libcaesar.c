#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include "libcaesar.h"

static unsigned char* secure_key_ptr = NULL;
static size_t page_size = 16; 

void memory_violation_handler(int sig, siginfo_t *si, void *unused) {
    (void)unused;
    if (si->si_addr >= (void*)secure_key_ptr && si->si_addr < (void*)(secure_key_ptr + page_size)) {
        fprintf(stderr, "\n[%s] Error: unauthorized try to write\n", sig == SIGBUS ? "SIGBUS" : "SIGSEGV");
        exit(1); 
    }
    
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(sig, &sa, NULL);
    raise(sig);
}

void caesar_key(unsigned char key)
{
    if (secure_key_ptr == NULL) {
        secure_key_ptr = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (secure_key_ptr == MAP_FAILED) {
            perror("Ошибка mmap");
            exit(1);
        }

        struct sigaction sa;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sa.sa_sigaction = memory_violation_handler;
        
        if (sigaction(SIGSEGV, &sa, NULL) == -1) {
            exit(1);
        }
        if (sigaction(SIGBUS, &sa, NULL) == -1) {
            exit(1);
        }
    }

    if (mprotect(secure_key_ptr, page_size, PROT_READ | PROT_WRITE) == -1) {
        perror("Ошибка mprotect (write)");
        exit(1);
    }

    secure_key_ptr[0] = key;

    if (mprotect(secure_key_ptr, page_size, PROT_READ) == -1) {
        perror("Ошибка mprotect (read)");
        exit(1);
    }
}

void caesar(void* src, void* dst, int len)
{
    if (!src || !dst || len <= 0 || !secure_key_ptr)
        return;

    if (mprotect(secure_key_ptr, page_size, PROT_READ | PROT_WRITE) == -1) {
        perror("Ошибка mprotect (temp unlock)");
        exit(1);
    }

    unsigned char local_key = secure_key_ptr[0];

    if (mprotect(secure_key_ptr, page_size, PROT_READ) == -1) {
        perror("Ошибка mprotect (temp lock)");
        exit(1);
    }

    unsigned char* s = (unsigned char*)src;
    unsigned char* d = (unsigned char*)dst;

    for (int i = 0; i < len; i++)
    {
        d[i] = s[i] ^ local_key;
    }
}

void trigger_security_violation() {
    if (secure_key_ptr) {
        printf("\n Attacking... \n");
        secure_key_ptr[0] = 0x99;
    }
}

__attribute__((destructor))
void cleanup_secure_memory() {
    if (secure_key_ptr && secure_key_ptr != MAP_FAILED) {
        mprotect(secure_key_ptr, page_size, PROT_READ | PROT_WRITE);
        memset(secure_key_ptr, 0, page_size); 
        mprotect(secure_key_ptr, page_size, PROT_READ); 
        munmap(secure_key_ptr, page_size);
    }
}
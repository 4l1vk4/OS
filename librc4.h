#ifndef LIBRC4_H
#define LIBRC4_H

#include <stddef.h>

typedef struct {
    unsigned char *state;
    size_t page_size;
    int i;
    int j;
} rc4_ctx_t;

rc4_ctx_t* rc4_init(const unsigned char *key, int key_len);
void rc4_crypt(rc4_ctx_t *ctx, unsigned char *data, int data_len);
void rc4_cleanup(rc4_ctx_t *ctx);

#endif
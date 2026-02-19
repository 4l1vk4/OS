#include <stdio.h>
#include <string.h>
#include "libcaesar.h"

static unsigned char current_key = 0;

void caesar_key(unsigned char key)
{
    current_key = key;
}

void caesar(void* src, void* dst, int len)
{
    if (!src || !dst || len <= 0)
        return;

    unsigned char* s = (unsigned char*)src;
    unsigned char* d = (unsigned char*)dst;

    for (int i = 0; i < len; i++)
    {
        d[i] = s[i] ^ current_key;
    }
}

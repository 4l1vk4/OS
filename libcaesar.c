#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Использование: %s <lib> <key> <src_file> <dst_file>\n", argv[0]);
        return 1;
    }

    unsigned char key = (unsigned char)atoi(argv[2]);
    char *src_path = argv[3];
    char *dst_path = argv[4];

    FILE *f_src = fopen(src_path, "rb");
    if (!f_src) {
        perror("Ошибка открытия исходного файла");
        return 1;
    }

    fseek(f_src, 0, SEEK_END);
    long f_size = ftell(f_src);
    rewind(f_src);

    void *buffer = malloc(f_size);
    if (!buffer) {
        printf("Ошибка выделения памяти\n");
        fclose(f_src);
        return 1;
    }

    fread(buffer, 1, f_size, f_src);
    fclose(f_src);

    caesar_key(key);
    caesar(buffer, buffer, (int)f_size);

    FILE *f_dst = fopen(dst_path, "wb");
    if (!f_dst) {
        perror("Ошибка создания выходного файла");
        free(buffer);
        return 1;
    }

    fwrite(buffer, 1, f_size, f_dst);
    
    fclose(f_dst);
    free(buffer);

    printf("Готово! Файл '%s' обработан ключом %d и сохранен в '%s'\n", src_path, key, dst_path);

    return 0;
}

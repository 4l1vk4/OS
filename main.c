#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

typedef void (*caesar_key_t)(unsigned char);
typedef void (*caesar_t)(void*, void*, int);

int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Использование: %s <lib_path> <key> <src_file> <dst_file>\n", argv[0]);
        return 1;
    }

    char *lib_path = argv[1];
    unsigned char key = (unsigned char)atoi(argv[2]);
    char *src_path = argv[3];
    char *dst_path = argv[4];

    void *handle = dlopen(lib_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Ошибка загрузки библиотеки: %s\n", dlerror());
        return 1;
    }

    caesar_key_t my_caesar_key = (caesar_key_t)dlsym(handle, "caesar_key");
    caesar_t my_caesar = (caesar_t)dlsym(handle, "caesar");

    if (!my_caesar_key || !my_caesar) {
        return 1;
    }

    FILE *f = fopen(src_path, "rb");
    if (!f) {
        perror("Ошибка открытия файла на чтение");
        dlclose(handle);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    void *buf = malloc(size);
    if (!buf) { fclose(f); dlclose(handle); return 1; }
    
    fread(buf, 1, size, f);
    fclose(f);

    my_caesar_key(key);
    my_caesar(buf, buf, (int)size);

    FILE *f_out = fopen(dst_path, "wb");
    if (!f_out) {
        perror("Ошибка открытия файла на запись");
        free(buf);
        dlclose(handle);
        return 1;
    }
    fwrite(buf, 1, size, f_out);
    fclose(f_out);
    free(buf);
    dlclose(handle);

    printf("Файл перезаписан\n");
    return 0;
}
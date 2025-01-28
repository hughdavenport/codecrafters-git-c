#define ZLIB_IMPLEMENTATION
#include "src/zlib.h"

int main() {
    int ret = 0;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define expect(expr) if (!(expr)) return_defer(1)
    char *object_file = ".git/objects/78/981922613b2afb6025042ff6bd878ac1994e85";
    FILE *file = NULL;
    file = fopen(object_file, "rb");
    expect(file != NULL);

    expect(fseek(file, 0L, SEEK_END) != -1);
    long size = ftell(file);
    expect(size != -1);
    expect(fseek(file, 0L, SEEK_SET) != -1);

    uint8_t *data = malloc(size);
    expect(data != NULL);
    expect(fread(data, size, 1, file) == 1);

    zlib_data_array decompressed = {0};

    zlib_decompress(data, size, &decompressed);

    fwrite(decompressed.data, 1, decompressed.size, stdout);

#undef return_defer
#undef expect
defer:
    if (data) free(data);
    if (file) fclose(file);
    if (decompressed.data) free(decompressed.data);
    return ret;
}

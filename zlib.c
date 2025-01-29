#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

#define ZLIB_IMPLEMENTATION
#define ZLIB_HEADER_FILE "src/zlib.h"
#include ZLIB_HEADER_FILE

int main(int argc, char **argv) {
    struct stat exe_stat, src_stat, zlib_stat;
    if (stat(argv[0], &exe_stat) == -1) return 1;
    if (stat(__FILE__, &src_stat) == -1) return 1;
    if (stat(ZLIB_HEADER_FILE, &zlib_stat) == -1) return 1;
    if (src_stat.st_mtime > exe_stat.st_mtime || zlib_stat.st_mtime > exe_stat.st_mtime) {
        fprintf(stderr, "Rebuilding\n");
        char *build_cmd = NULL;
        if (asprintf(&build_cmd,
                    "cc -Wall -Werror -Wextra -Wpedantic -fsanitize=address -ggdb -g -O0 %s -o %s",
                    __FILE__,
                    argv[0]) == -1) return 1;
        if (system(build_cmd) != 0) return 1;
        free(build_cmd);
        execve(argv[0], argv, environ);
        UNREACHABLE();
    }
    int ret = 0;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define expect(expr) if (!(expr)) return_defer(1)
    zlib_context ctx = {0};
    FILE *file = stdin;
    uint8_t *data = NULL;
    if (argc > 1) {
        file = fopen(argv[1], "rb");
    }
    expect(file != NULL);

    /* expect(fseek(file, 0L, SEEK_END) != -1); */
    /* long size = ftell(file); */
    /* expect(size != -1); */
    /* expect(fseek(file, 0L, SEEK_SET) != -1); */

    /* data = malloc(size); */
    /* expect(data != NULL); */
    /* expect(fread(data, size, 1, file) == 1); */

    /* ctx.deflate.bits.data = data; */
    /* ctx.deflate.bits.size = size; */

    /* zlib_decompress(&ctx); */

    uint8_t buf[8000] = {0};
    do {
        if (DEFLATE_BYTES(&ctx.deflate) > 0) {
            memcpy(buf, ctx.deflate.bits.data, DEFLATE_BYTES(&ctx.deflate));
        }
        ssize_t read_bytes = fread(buf + DEFLATE_BYTES(&ctx.deflate),
                                   1,
                                   C_ARRAY_LEN(buf) - DEFLATE_BYTES(&ctx.deflate),
                                   file);
        if (read_bytes == 0) {
            fprintf(stderr, "EOF\n");
            return 1;
        }
        ctx.deflate.bits.size = DEFLATE_BYTES(&ctx.deflate) + read_bytes;
        ctx.deflate.bits.data = buf;
    } while (!zlib_decompress(&ctx));

    fwrite(ctx.deflate.decompressed.data, 1, ctx.deflate.decompressed.size, stdout);

#undef return_defer
#undef expect
defer:
    if (data) free(data);
    if (file && file != stdin) fclose(file);
    if (ctx.deflate.decompressed.data) free(ctx.deflate.decompressed.data);
    return ret;
}

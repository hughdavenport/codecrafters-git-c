#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define SHA1_IMPLEMENTATION
#include "sha1.h"
#define ZLIB_IMPLEMENTATION
#include "zlib.h"
#define ARG() *argv++; argc--

bool file_read_contents(const char *filename, uint8_t **data, long *size) {
    bool ret = true;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define expect(expr) if (!(expr)) return_defer(false)
    FILE *file = NULL;
    file = fopen(filename, "rb");
    expect(file != NULL);

    expect(fseek(file, 0L, SEEK_END) != -1);
    *size = ftell(file);
    expect(*size != -1);
    expect(fseek(file, 0L, SEEK_SET) != -1);

    *data = malloc(*size);
    expect(*data != NULL);
    expect(fread(*data, *size, 1, file) == 1);

#undef return_defer
#undef expect
defer:
    if (file) fclose(file);
    if (!ret) {
        if (*data) free(*data);
    }
    return ret;
}

int init_command(int argc, char *argv[]) {
    int dir_fd = AT_FDCWD;
    char *dir = NULL;
    if (argc > 0) {
        dir = ARG();
        dir_fd = openat(AT_FDCWD, dir, O_DIRECTORY);
        if (dir_fd == -1) {
            if (mkdirat(AT_FDCWD, dir, 0755) == -1) {
                fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
                return 1;
            }
            dir_fd = openat(AT_FDCWD, dir, O_DIRECTORY);
            if (dir_fd == -1) {
                fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
                return 1;
            }
        }
    }
    if (mkdirat(dir_fd, ".git", 0755) == -1 ||
        mkdirat(dir_fd, ".git/objects", 0755) == -1 ||
        mkdirat(dir_fd, ".git/refs", 0755) == -1) {
        fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
        return 1;
    }

    int head_fd = openat(dir_fd, ".git/HEAD", O_CREAT|O_TRUNC|O_WRONLY, 0644);
    if (head_fd == -1) {
        fprintf(stderr, "Failed to create .git/HEAD file: %s\n", strerror(errno));
        return 1;
    }
    FILE *headFile = fdopen(head_fd, "w");
    if (headFile == NULL) {
        fprintf(stderr, "Failed to create .git/HEAD file: %s\n", strerror(errno));
        return 1;
    }
    fprintf(headFile, "ref: refs/heads/main\n");
    fclose(headFile); // will close head_fd

    if (dir_fd != AT_FDCWD) {
        close(dir_fd);
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, PATH_MAX) == NULL) {
        if (dir == NULL) {
            printf("Initialized empty Git repository\n");
        } else {
            printf("Initialized empty Git repository in %s/.git/\n", dir);
        }
    }
    if (dir == NULL) {
        printf("Initialized empty Git repository in %s/.git/\n", cwd);
    } else {
        printf("Initialized empty Git repository in %s/%s/.git/\n", cwd, dir);
    }
    return 0;
}

int cat_file_command(int argc, char *argv[]) {
    int ret = 0;
    uint8_t *filedata = NULL;
    char *object_path = NULL;
    zlib_context ctx = {0};
#define return_defer(code) do { ret = (code); goto defer; } while (0);
    bool pretty = false; (void)pretty;
    if (argc <= 0) {
        // FIXME display error
        return_defer(1);
    }
    char *arg = ARG();
    if (strcmp(arg, "-p") == 0) {
        pretty = true;
        if (argc <= 0) {
            // FIXME display error
            return_defer(1);
        }
        arg = ARG();
    }
    // FIXME check valid hash
    if (strlen(arg) != 40) {

        // FIXME display error
        return_defer(1);
    }

    // FIXME check in right dir
    object_path = malloc(55);
    if (object_path == NULL) {
        fprintf(stderr, "Ran out of memory creating object path\n");
        return_defer(1);
    }
    char *objects_dir = ".git/objects";
    if (sprintf(object_path, "%s/xx/%38s", objects_dir, arg + 2) == -1) {
        fprintf(stderr, "Ran out of memory creating object path\n");
        return_defer(1);
    }
    object_path[strlen(objects_dir)+1] = arg[0];
    object_path[strlen(objects_dir)+2] = arg[1];

    long size = 0;
    if (!file_read_contents(object_path, &filedata, &size)) {
        fprintf(stderr, "Couldn't read object file at '%s'\n", object_path);
        return_defer(1);
    }

    /* hexdump(filedata, size); */

    ctx.deflate.bits.data = filedata;
    ctx.deflate.bits.size = size;

    if (!zlib_decompress(&ctx)) {
        fprintf(stderr, "Couldn't decompress object file at '%s'\n", object_path);
        return_defer(1);
    }

    if (ctx.deflate.decompressed.size < 5) {
        fprintf(stderr, "Decompressed data too small\n");
        return_defer(1);
    }
    if (strncmp((char*)ctx.deflate.decompressed.data, "blob ", 5) != 0) {
        fprintf(stderr, "Decompressed data is not a valid blob object\n");
        return_defer(1);
    }
    if (!isdigit((char)ctx.deflate.decompressed.data[5])) {
        fprintf(stderr, "Decompressed data is not a valid blob object\n");
        return_defer(1);
    }

    char *blob_head_end = NULL;
    long blob_size = strtol((char*)ctx.deflate.decompressed.data + 5, &blob_head_end, 10);
    if (*blob_head_end != '\0') {
        fprintf(stderr, "Invalid blob size\n");
        return_defer(1);
    }
    assert(blob_size == (char*)(ctx.deflate.decompressed.data + ctx.deflate.decompressed.size) - blob_head_end - 1);
    fwrite(blob_head_end + 1, 1, blob_size, stdout);
#undef return_defer
defer:
    if (filedata != NULL) free(filedata);
    if (object_path != NULL) free(object_path);
    if (ctx.deflate.decompressed.data) free(ctx.deflate.decompressed.data);
    return ret;
}

int hash_object_command(int argc, char *argv[]) {
    int ret = 0;
    FILE *file = NULL;
    uint8_t *filedata = NULL;
    uint8_t *blob = NULL;
    int dir_fd = AT_FDCWD;
    int blob_fd = -1;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
    bool writeblob = false;
    char *filename = NULL;
    while (argc > 0) {
        char *arg = ARG();
        if (strcmp(arg, "-w") == 0) {
            writeblob = true;
        } else {
            filename = arg;
        }
    }

    if (filename == NULL) {
        // FIXME display error
        return_defer(1);
    }

    long size = 0;
    if (!file_read_contents(filename, &filedata, &size)) {
        // FIXME display error
        return_defer(1);
    }

    long bloblen = 0;
    long tmp = size;
    while (tmp != 0) {
        bloblen ++;
        tmp /= 10;
    }

    // blob SP bloblen NULL filedata
    long blobsize = size + bloblen + 6;
    blob = malloc(blobsize);
    if (blob == NULL) {
        // FIXME display error
        return_defer(1);
    }

    long headersize = snprintf((char *)blob, blobsize, "blob %ld", size);
    assert(headersize == bloblen + 5);
    assert(blob[headersize] == '\0');

    memcpy(blob + headersize + 1, filedata, size);

    uint8_t result[SHA1_DIGEST_BYTE_LENGTH];

    if (!sha1_digest(blob, blobsize, result)) {
        // FIXME display error
        return_defer(1);
    }

    if (writeblob) {
        // mkdir .git/objects
        // TODO different git dir locations
        // TODO find parent dir if within the git file system
        if ((mkdirat(dir_fd, ".git", 0755) == -1 && errno != EEXIST) ||
            (mkdirat(dir_fd, ".git/objects", 0755) == -1 && errno != EEXIST)) {
            // FIXME display error
            return_defer(1);
        }
        int fd = openat(dir_fd, ".git/objects", O_DIRECTORY);
        if (dir_fd != AT_FDCWD) close(dir_fd);
        dir_fd = fd;

        char byte1[3];
        assert(snprintf(byte1, 3, "%02x", result[0]) == 2);
        if (mkdirat(dir_fd, byte1, 0755) == -1 && errno != EEXIST) {
            // FIXME display error
            return_defer(1);
        }
        fd = openat(dir_fd, byte1, O_DIRECTORY);
        if (dir_fd != AT_FDCWD) close(dir_fd);
        dir_fd = fd;

        char rest[2 * (SHA1_DIGEST_BYTE_LENGTH - 1) + 1];
        for (int idx = 1; idx < SHA1_DIGEST_BYTE_LENGTH; idx ++) {
            assert(snprintf(rest + (idx - 1) * 2, 3, "%02x", result[idx]) == 2);
        }

        int blob_fd = openat(dir_fd, rest, O_CREAT|O_TRUNC|O_WRONLY, 0644);
        if (blob_fd == -1) {
            // FIXME display error
            return_defer(1);
        }

        // FIXME compress the thing
        assert(write(blob_fd, blob, blobsize) == blobsize);
    }

    for (int idx = 0; idx < SHA1_DIGEST_BYTE_LENGTH; idx ++) {
        printf("%02x", result[idx]);
    }
    printf("\n");

#undef return_defer
defer:
    if (file != NULL) fclose(file);
    if (filedata != NULL) free(filedata);
    if (blob != NULL) free(blob);
    if (dir_fd != AT_FDCWD) close(dir_fd);
    if (blob_fd != -1) close(blob_fd);
    return ret;
}

int main(int argc, char *argv[]) {

    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 2) {
        fprintf(stderr, "Usage: ./your_program.sh <command> [<args>]\n");
        return 1;
    }

    const char *program = ARG(); (void)program;
    const char *command = ARG();

    if (strcmp(command, "init") == 0) {
        return init_command(argc, argv);
    } else if (strcmp(command, "cat-file") == 0) {
        return cat_file_command(argc, argv);
    } else if (strcmp(command, "hash-object") == 0) {
        return hash_object_command(argc, argv);
    } else {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }

    return 0;
}

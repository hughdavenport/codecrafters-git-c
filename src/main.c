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

bool read_object(char *hash, uint8_t **data, long *size) {
    bool ret = false;
    zlib_context ctx = {0};
    char *object_path = NULL;
    uint8_t *filedata = NULL;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
    object_path = malloc(55);
    if (object_path == NULL) {
        fprintf(stderr, "Ran out of memory creating object path\n");
        return_defer(false);
    }
    char *objects_dir = ".git/objects";
    if (sprintf(object_path, "%s/xx/%38s", objects_dir, hash + 2) == -1) {
        UNREACHABLE();
        return_defer(false);
    }
    object_path[strlen(objects_dir)+1] = hash[0];
    object_path[strlen(objects_dir)+2] = hash[1];

    long filesize = 0;
    // FIXME check in right dir
    if (!file_read_contents(object_path, &filedata, &filesize)) {
        fprintf(stderr, "Couldn't read file %s\n", object_path);
        return_defer(false);
    }

    /* hexdump(filedata, size); */

    ctx.deflate.bits.data = filedata;
    ctx.deflate.bits.size = filesize;

    if (!zlib_decompress(&ctx)) {
        fprintf(stderr, "Couldn't decompress object file %s\n", object_path);
        return_defer(false);
    }

    *data = ctx.deflate.out.data;
    *size = ctx.deflate.out.size;
    ret = true;

#undef return_defer
defer:
    if (filedata) free(filedata);
    if (object_path) free(object_path);
    return ret;
}

int init_command(const char *program, int argc, char *argv[]) {
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

typedef enum {
    UNKNOWN,
    BLOB,
    TREE,

    NUM_OBJECTS, // Keep at end, _Static_assert's depend on it
} git_object_t;

int cat_file_command(const char *program, int argc, char *argv[]) {
    int ret = 0;
    uint8_t *data = NULL;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define usage() do { \
    fprintf(stderr, "usage: %s cat-file (-p|-t) <sha1-hash>\n", program); \
} while (0)

    if (argc <= 0) {
        usage();
        return_defer(1);
    }
    bool pretty = false;
    bool showtype = false;
    char *hash = NULL;
    git_object_t type = UNKNOWN;
    while (argc > 0) {
        char *arg = ARG();
        if (strcmp(arg, "-p") == 0) {
            pretty = true;
        } else if (strcmp(arg, "-t") == 0) {
            showtype = true;
        } else {
            _Static_assert(NUM_OBJECTS == 3, "Objects have changed. May need handling here");
            if (strcmp(arg, "blob") == 0) {
                type = BLOB;
            } else if (strcmp(arg, "tree") == 0) {
                type = TREE;
            } else {
                // FIXME get the hash from an object name (i.e. HEAD)
                hash = arg;
            }
        }
    }

    if (pretty && showtype) {
        fprintf(stderr, "ERROR: -p is incompatible with -t\n");
        return_defer(1);
    }
    if (!pretty && !showtype && type == UNKNOWN) {
        usage();
        return_defer(1);
    }
    if (showtype && type != UNKNOWN) {
        usage();
        return_defer(1);
    }
    if (hash == NULL) {
        usage();
        return_defer(1);
    }
    if (strlen(hash) != 2 * SHA1_DIGEST_BYTE_LENGTH) {
        fprintf(stderr, "ERROR: %s is not a valid SHA-1 hash\n", hash);
        return_defer(1);
    }
    for (size_t i = 0; i < 2 * SHA1_DIGEST_BYTE_LENGTH; i ++) {
        if (isxdigit(hash[i]) == 0) {
            fprintf(stderr, "ERROR: %s is not a valid SHA-1 hash\n", hash);
            return_defer(1);
        }
    }

    long filesize = 0;
    if (!read_object(hash, &data, &filesize)) {
        fprintf(stderr, "Couldn't read object file %s\n", hash);
        return_defer(1);
    }

    char *header_end = NULL;
    long size = 0;
    git_object_t object_type = UNKNOWN;
    _Static_assert(NUM_OBJECTS == 3, "Objects have changed. May need handling here");
    if (filesize > 5 && strncmp((char *)data, "blob ", 5) == 0) {
        object_type = BLOB;
        if (showtype) {
            printf("blob\n");
            return_defer(0);
        }
        size = strtol((char *)data + 5, &header_end, 10);
    } else if (filesize > 5 && strncmp((char *)data, "tree ", 5) == 0) {
        object_type = TREE;
        if (showtype) {
            printf("tree\n");
            return_defer(0);
        }
        size = strtol((char *)data + 5, &header_end, 10);
    } else {
        fprintf(stderr, "Decompressed data is not a valid object\n");
        return_defer(1);
    }

    if (type != UNKNOWN && object_type != type) {
        fprintf(stderr, "Invalid type in object file %s\n", hash);
        return_defer(1);
    }

    if (*header_end != '\0') {
        fprintf(stderr, "Invalid size in object file at %s\n", hash);
        return_defer(1);
    }

    _Static_assert(NUM_OBJECTS == 3, "Objects have changed. May need handling here");
    switch (object_type) {
        case UNKNOWN:
            fprintf(stderr, "Decompressed data is not a valid object\n");
            return_defer(1);
            break;

        case BLOB:
            assert(size == (char*)(data + filesize) - header_end - 1);
            assert(fwrite(header_end + 1, 1, size, stdout) == size);
            break;

        case TREE: {
            assert(size == (char*)(data + filesize) - header_end - 1);
            if (type == TREE) {
                assert(fwrite(header_end + 1, 1, size, stdout) == size);
                return_defer(0);
            }
            char *end = (char *)data + filesize;
            char *p = header_end + 1;
            while (p < end) {
                char *start = p;
                while (p < end && *p != '\0') {
                    p ++;
                }
                assert(p + SHA1_DIGEST_BYTE_LENGTH + 1 <= end);
                uint8_t *hash = (uint8_t*)p + 1;
                p += SHA1_DIGEST_BYTE_LENGTH + 1;
                char *file = NULL;
                long mode = strtol(start, &file, 8);
                assert(*file == ' ');
                file ++;
                printf("%06lo", mode);
                switch (mode & S_IFMT) {
                    case S_IFDIR: printf(" tree "); break;
                    case S_IFREG: printf(" blob "); break;

                    case S_IFBLK:
                    case S_IFCHR:
                    case S_IFIFO:
                    case S_IFLNK:
                    case S_IFSOCK:
                    default:
                        UNREACHABLE();
                }
                for (size_t i = 0; i < SHA1_DIGEST_BYTE_LENGTH; i ++) {
                    printf("%02x", hash[i]);
                }
                printf("    %s\n", file);
            }
        }; break;


        default:
            UNREACHABLE();
            return_defer(1);
    }

#undef usage
#undef return_defer
defer:
    if (data) free(data);
    return ret;
}

int hash_object_command(const char *program, int argc, char *argv[]) {
    int ret = 0;
    FILE *file = NULL;
    uint8_t *filedata = NULL;
    uint8_t *blob = NULL;
    int dir_fd = AT_FDCWD;
    int blob_fd = -1;
    zlib_context ctx = {0};
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define usage() do { \
    fprintf(stderr, "usage: %s hash-object [-w] <filename>\n", program); \
} while (0)

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
        usage();
        return_defer(1);
    }

    long size = 0;
    if (!file_read_contents(filename, &filedata, &size)) {
        fprintf(stderr, "Error reading file %s\n", filename);
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
        fprintf(stderr, "Ran out of memory creating blob\n");
        return_defer(1);
    }

    long headersize = snprintf((char *)blob, blobsize, "blob %ld", size);
    assert(headersize == bloblen + 5);
    assert(blob[headersize] == '\0');

    memcpy(blob + headersize + 1, filedata, size);

    uint8_t result[SHA1_DIGEST_BYTE_LENGTH];

    if (!sha1_digest(blob, blobsize, result)) {
        fprintf(stderr, "Error while creating SHA1 digest of blob\n");
        return_defer(1);
    }

    if (writeblob) {
        // mkdir .git/objects
        // TODO different git dir locations
        // TODO find parent dir if within the git file system
        if ((mkdirat(dir_fd, ".git", 0755) == -1 && errno != EEXIST) ||
            (mkdirat(dir_fd, ".git/objects", 0755) == -1 && errno != EEXIST)) {
            fprintf(stderr, "Failed to create directory: .git/objects: %s\n", strerror(errno));
            return_defer(1);
        }
        int fd = openat(dir_fd, ".git/objects", O_DIRECTORY);
        if (dir_fd != AT_FDCWD) close(dir_fd);
        dir_fd = fd;

        char byte1[3];
        assert(snprintf(byte1, 3, "%02x", result[0]) == 2);
        if (mkdirat(dir_fd, byte1, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "Failed to create directory: .git/objects/%s: %s\n", byte1, strerror(errno));
            return_defer(1);
        }
        fd = openat(dir_fd, byte1, O_DIRECTORY);
        if (dir_fd != AT_FDCWD) close(dir_fd);
        dir_fd = fd;

        char rest[2 * (SHA1_DIGEST_BYTE_LENGTH - 1) + 1];
        for (int idx = 1; idx < SHA1_DIGEST_BYTE_LENGTH; idx ++) {
            assert(snprintf(rest + (idx - 1) * 2, 3, "%02x", result[idx]) == 2);
        }

        int unlink_ret = unlinkat(dir_fd, rest, 0);
        if (unlink_ret == -1) {
            assert(errno == ENOENT);
        }
        int blob_fd = openat(dir_fd, rest, O_CREAT|O_TRUNC|O_WRONLY, 0644);
        if (blob_fd == -1) {
            fprintf(stderr, "Failed to create object file: .git/objects/%s/%s: %s\n", byte1, rest, strerror(errno));
            return_defer(1);
        }

        ctx.deflate.in.data = blob;
        ctx.deflate.in.size = blobsize;
        if (!zlib_compress(&ctx)) {
            fprintf(stderr, "Error while zlib compressing the blob\n");
            return_defer(1);
        }
        assert(write(blob_fd, ctx.deflate.out.data, ctx.deflate.out.size) == ctx.deflate.out.size);
    }

    for (int idx = 0; idx < SHA1_DIGEST_BYTE_LENGTH; idx ++) {
        printf("%02x", result[idx]);
    }
    printf("\n");

#undef usage
#undef return_defer
defer:
    if (file != NULL) fclose(file);
    if (filedata != NULL) free(filedata);
    if (blob != NULL) free(blob);
    if (dir_fd != AT_FDCWD) close(dir_fd);
    if (blob_fd != -1) close(blob_fd);
    if (ctx.deflate.out.data) free(ctx.deflate.out.data);
    return ret;
}

int ls_tree_command(const char *program, int argc, char *argv[]) {
    int ret = 0;
    uint8_t *data = NULL;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define usage() do { \
    fprintf(stderr, "usage: %s ls-tree [--name-only] <sha1-hash>\n", program); \
} while (0)

    if (argc <= 0) {
        usage();
        return_defer(1);
    }

    bool name_only = false;
    bool object_only = false;
    char *hash = NULL;
    while (argc > 0) {
        char *arg = ARG();
        if (strcmp(arg, "--name-only") == 0) {
            name_only = true;
        } else if (strcmp(arg, "--object-only") == 0) {
            object_only = true;
        } else {
            // FIXME get the hash from an object name (i.e. HEAD)
            hash = arg;
        }
    }

    if (name_only && object_only) {
        fprintf(stderr, "ERROR: --name-only is incompatible with --object-only\n");
        return_defer(1);
    }

    if (hash == NULL) {
        usage();
        return_defer(1);
    }
    if (strlen(hash) != 2 * SHA1_DIGEST_BYTE_LENGTH) {
        fprintf(stderr, "ERROR: %s is not a valid SHA-1 hash\n", hash);
        return_defer(1);
    }
    for (size_t i = 0; i < 2 * SHA1_DIGEST_BYTE_LENGTH; i ++) {
        if (isxdigit(hash[i]) == 0) {
            fprintf(stderr, "ERROR: %s is not a valid SHA-1 hash\n", hash);
            return_defer(1);
        }
    }

    long filesize = 0;
    if (!read_object(hash, &data, &filesize)) {
        fprintf(stderr, "Couldn't read object file %s\n", hash);
        return_defer(1);
    }

    char *header_end = NULL;
    long size = 0;
    _Static_assert(NUM_OBJECTS == 3, "Objects have changed. May need handling here");
    if (filesize <= 5 || strncmp((char *)data, "tree ", 5) != 0) {
        fprintf(stderr, "Object is not a tree: %s\n", hash);
        return_defer(1);
    }

    size = strtol((char *)data + 5, &header_end, 10);
    if (*header_end != '\0') {
        fprintf(stderr, "Invalid size in object file at %s\n", hash);
        return_defer(1);
    }
    assert(size == (char*)(data + filesize) - header_end - 1);

    char *end = (char *)data + filesize;
    char *p = header_end + 1;
    while (p < end) {
        char *start = p;
        while (p < end && *p != '\0') {
            p ++;
        }
        assert(p + SHA1_DIGEST_BYTE_LENGTH + 1 <= end);
        uint8_t *hash = (uint8_t*)p + 1;
        p += SHA1_DIGEST_BYTE_LENGTH + 1;
        char *file = NULL;
        long mode = strtol(start, &file, 8);
        assert(*file == ' ');
        file ++;
        if (!name_only && !object_only) {
            printf("%06lo", mode);
            switch (mode & S_IFMT) {
                case S_IFDIR: printf(" tree "); break;
                case S_IFREG: printf(" blob "); break;

                case S_IFBLK:
                case S_IFCHR:
                case S_IFIFO:
                case S_IFLNK:
                case S_IFSOCK:
                default:
                    UNREACHABLE();
            }
        }
        if (!name_only) {
            for (size_t i = 0; i < SHA1_DIGEST_BYTE_LENGTH; i ++) {
                printf("%02x", hash[i]);
            }
        }
        if (!name_only && !object_only) {
            printf("    ");
        }
        if (!object_only) {
            printf("%s", file);
        }
        printf("\n");
    }

#undef usage
#undef return_defer
defer:
    if (data) free(data);
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
        return init_command(program, argc, argv);
    } else if (strcmp(command, "cat-file") == 0) {
        return cat_file_command(program, argc, argv);
    } else if (strcmp(command, "hash-object") == 0) {
        return hash_object_command(program, argc, argv);
    } else if (strcmp(command, "ls-tree") == 0) {
        return ls_tree_command(program, argc, argv);
    } else {
        // FIXME work out similar commands
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }

    return 0;
}

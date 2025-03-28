#define _GNU_SOURCE
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>

#define SHA1_IMPLEMENTATION
#include "sha1.h"
#define ZLIB_IMPLEMENTATION
#include "zlib.h"

#include <signal.h>
#define BREAKPOINT() do { raise(SIGTRAP); } while (false)

#define GIT_UNREACHABLE() do { fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__); fflush(stderr); BREAKPOINT(); abort(); } while (0)
#define GIT_UNIMPLENTED(fmt, ...) do { fprintf(stderr, "%s:%d: UNIMPLENTED %s: " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); fflush(stderr); BREAKPOINT(); abort(); } while (0)
#define ARG() *argv++; argc--

#define C_ARRAY_LEN(arr) (sizeof((arr))/(sizeof((arr)[0])))

#define GIT_DIR_MODE  0040000
#define GIT_EXE_MODE  0100755
#define GIT_FILE_MODE 0100644

typedef enum {
    ARG_END,
    BOOL,
    STRING,
    OBJECT_TYPE,
    OBJECT_HASH,

    CONFLICT,
    REQUIRED_IN_ORDER,

    NUM_ARG_TYPES, // Keep at end, _Static_assert's depend on it
} command_arg_typ_t;

typedef struct {
    const char *name;
    void *data;
    int matched;
    command_arg_typ_t typ;
} command_arg_t;

#define ARGS(...) (command_arg_t[]) { \
    __VA_ARGS__, \
    {0}, \
}

#define CONFLICTS(...) { .typ = CONFLICT, .data = (void *)ARGS(__VA_ARGS__) }
#define REQUIRES(...) { .typ = REQUIRED_IN_ORDER, .data = (void *)ARGS(__VA_ARGS__) }

#define FLAG(flagname) { .typ = BOOL, .name = (flagname) }

typedef struct command_t {
    char *name;
    command_arg_t *args;
    int (*func)(struct command_t *cmd, const char *program, int argc, char **argv);
} command_t;

bool parse_args(command_t *command, int argc, char **argv) {
    assert(command->args != NULL);
    command_arg_t *arg_p = NULL;
    for (size_t i = 0; i < (size_t)argc; ++ i) {
        bool match = false;
        for (arg_p = command->args; arg_p->typ != ARG_END; arg_p ++) {
            _Static_assert(NUM_ARG_TYPES == 7, "Arg type have changed. May need handling here");
            switch (arg_p->typ) {
                case BOOL:
                    if (arg_p->name && strcmp(arg_p->name, argv[i]) == 0) {
                        arg_p->data = (void *)true;
                        match = true;
                    }
                    break;

                case STRING:
                    GIT_UNIMPLENTED();
                    break;

                case OBJECT_TYPE:
                        /* if (strcmp(argv[i], "blob") == 0) { */
                        /*     type = BLOB; */
                        /* } else if (strcmp(argv[i], "tree") == 0) { */
                        /*     type = TREE; */
                        /* } else { */
                        /*     match = false; */
                        /* } */
                        /* if (match) { */
                        /*     // FIXME go and check other args ahead to see if this matches */
                        /* } */
                    GIT_UNIMPLENTED();
                    break;

                case OBJECT_HASH:
                    GIT_UNIMPLENTED();
                    break;

                case CONFLICT:
                    GIT_UNIMPLENTED();
                    break;

                case REQUIRED_IN_ORDER:
                    GIT_UNIMPLENTED();
                    break;

                default:
                    GIT_UNREACHABLE();
                    return false;
            }
        }
        if (!match) {
            assert(arg_p != NULL && arg_p->typ == ARG_END);
            arg_p->data = (void *)i;
            return false;
        }
    }
    for (arg_p = command->args; arg_p->typ != ARG_END; arg_p ++);
    assert(arg_p != NULL && arg_p->typ == ARG_END);
    arg_p->data = (void *)(size_t)argc;
    return true;
}

int help_command(command_t *command, const char *program, int argc, char **argv);

typedef struct {
    size_t size;
    size_t capacity;
    uint8_t *data;
} uint8_array_t;

typedef struct {
    size_t size;
    size_t capacity;
    char **data;
} string_array_t;

#define ARRAY_ENSURE(arr, inc) do { \
    if ((arr).size + (inc) > (arr).capacity) { \
        size_t new_cap = (arr).capacity == 0 ? 16 : (arr).capacity * 2; \
        while (new_cap < (arr).size + (inc)) new_cap *= 2; \
        (arr).data = realloc((arr).data, new_cap * sizeof((arr).data[0])); \
        assert((arr).data != NULL); \
        (arr).capacity = new_cap; \
    } \
} while (0)

#define ARRAY_APPEND(arr, v) do { \
    ARRAY_ENSURE(arr, 1); \
    (arr).data[(arr).size ++] = (v); \
} while (0)

#define ARRAY_APPEND_BYTES(arr, src, len) do { \
    ARRAY_ENSURE(arr, (len)); \
    memcpy((arr).data + (arr).size, (src), (len)); \
    (arr).size += (len); \
} while (0)

bool file_read_contents_fp(FILE *file, uint8_t **data, long *size) {
    bool ret = true;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define expect(expr) if (!(expr)) return_defer(false)

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
    if (!ret) {
        if (*data) free(*data);
    }
    return ret;
}

bool file_read_contents(const char *filename, uint8_t **data, long *size) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) return false;
    bool ret = file_read_contents_fp(file, data, size);
    fclose(file);
    return ret;
}

bool file_read_contents_at(int dir_fd, const char *filename, uint8_t **data, long *size) {
    int fd = openat(dir_fd, filename, O_RDONLY);
    assert(fd > 0);
    FILE *file = fdopen(fd, "rb");
    if (file == NULL) {
        close(fd);
        return false;
    }
    bool ret = file_read_contents_fp(file, data, size);
    fclose(file);
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
        GIT_UNREACHABLE();
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

int init_command(command_t *command, const char *program, int argc, char *argv[]) {
    /* FIXME make an arg string? */
    (void)command;
    (void)program;
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
    COMMIT,

    NUM_OBJECTS, // Keep at end, _Static_assert's depend on it
} git_object_t;

// blob blob
//
//         ^
// {
//  requires
//     conflicts                 matches (blob #1, blob #2)
//        conflicts              no match
//          -p       no
//          -t       no
//        type       yes (blob #1, blob #2)
//     object        yes (blob #1, blob #2)
//
// argc
// 1-  000000000001
// 2-  0         10
// 3-  0        100
// argc 10000000 00000000 00000000 00000000
// FIXME need assertion that argc <= 32
//
#define cat_file_args ARGS( \
    REQUIRES( \
        CONFLICTS( \
            CONFLICTS(FLAG("-p"), FLAG("-t")), \
            { .typ = OBJECT_TYPE } \
        ), \
        { .typ = OBJECT_HASH } \
    ) \
)
int cat_file_command(command_t *command, const char *program, int argc, char *argv[]) {
    int ret = 0;
    uint8_t *data = NULL;
    /* assert(cat_file_args[C_ARRAY_LEN(cat_file_args) - 1].typ == ARG_END && */
    /*         "Last element of the args needs to be {0}"); */
    /* assert(cat_file_args[C_ARRAY_LEN(cat_file_args) - 1].data == NULL && */
    /*         "Command is only expected to be run once"); */
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define usage() do { \
    char *help_argv[] = { command->name }; \
    help_command(NULL, program, 1, help_argv); \
} while (0)

    /* if (!parse_args(command, argc, argv)) { */
    /*     usage(); */
    /*     return_defer(1); */
    /* } */

    /* BREAKPOINT(); */

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
            _Static_assert(NUM_OBJECTS == 4, "Objects have changed. May need handling here");
            if (strcmp(arg, "blob") == 0) {
                type = BLOB;
            } else if (strcmp(arg, "tree") == 0) {
                type = TREE;
            } else if (strcmp(arg, "commit") == 0) {
                type = COMMIT;
            } else {
                if (*arg == '-') {
                    usage();
                    return_defer(1);
                }
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
    if (strlen(hash) != SHA1_DIGEST_HEX_LENGTH) {
        fprintf(stderr, "ERROR: %s is not a valid SHA-1 hash\n", hash);
        return_defer(1);
    }
    for (size_t i = 0; i < SHA1_DIGEST_HEX_LENGTH; i ++) {
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
    _Static_assert(NUM_OBJECTS == 4, "Objects have changed. May need handling here");
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
    } else if (filesize > 5 && strncmp((char *)data, "commit ", 7) == 0) {
        object_type = COMMIT;
        if (showtype) {
            printf("commit\n");
            return_defer(0);
        }
        size = strtol((char *)data + 7, &header_end, 10);
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

    _Static_assert(NUM_OBJECTS == 4, "Objects have changed. May need handling here");
    switch (object_type) {
        case UNKNOWN:
            fprintf(stderr, "Decompressed data is not a valid object\n");
            return_defer(1);
            break;

        case BLOB:
        case COMMIT:
            assert(size == (char*)(data + filesize) - header_end - 1);
            assert(fwrite(header_end + 1, 1, size, stdout) == (size_t)size);
            break;

        case TREE: {
            assert(size == (char*)(data + filesize) - header_end - 1);
            if (type == TREE) {
                assert(fwrite(header_end + 1, 1, size, stdout) == (size_t)size);
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
                        GIT_UNREACHABLE();
                }
                SHA1_PRINTF_HEX(hash);
                printf("    %s\n", file);
            }
        }; break;


        default:
            GIT_UNREACHABLE();
            return_defer(1);
    }

#undef usage
#undef return_defer
defer:
    if (data) free(data);
    return ret;
}

bool hash_object(git_object_t type, uint8_t *data, size_t size, uint8_t hash[SHA1_DIGEST_BYTE_LENGTH], bool writeobject) {
    bool ret = true;
    uint8_t *object = NULL;
    int dir_fd = AT_FDCWD;
    zlib_context ctx = {0};
#define return_defer(code) do { ret = (code); goto defer; } while (0);

    long numlen = 0;
    long tmp = size;
    while (tmp != 0) {
        numlen ++;
        tmp /= 10;
    }

    // type SP size NULL filedata
    long objectsize = size + numlen + 1;
    _Static_assert(NUM_OBJECTS == 4, "Objects have changed. May need handling here");
    switch (type) {
        case BLOB:
        case TREE:
            objectsize += 5;
            break;

        case COMMIT:
            objectsize += 7;
            break;

        default:
            GIT_UNREACHABLE();
            return_defer(false);
    }
    object = malloc(objectsize);
    if (object == NULL) {
        fprintf(stderr, "Ran out of memory creating object\n");
        return_defer(1);
    }

    long headersize = 0;
    _Static_assert(NUM_OBJECTS == 4, "Objects have changed. May need handling here");
    switch (type) {
        case BLOB:
            headersize = snprintf((char *)object, objectsize, "blob %ld", size);
            assert(headersize == numlen + 5);
            break;

        case TREE:
            headersize = snprintf((char *)object, objectsize, "tree %ld", size);
            assert(headersize == numlen + 5);
            break;

        case COMMIT:
            headersize = snprintf((char *)object, objectsize, "commit %ld", size);
            assert(headersize == numlen + 7);
            break;


        default:
            GIT_UNREACHABLE();
            return_defer(false);
    }
    assert(object[headersize] == '\0');

    memcpy(object + headersize + 1, data, size);

/* hexdump(object, objectsize); */
    if (!sha1_digest(object, objectsize, hash)) {
        fprintf(stderr, "Error while creating SHA1 digest of object\n");
        return_defer(1);
    }

    if (writeobject) {
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

        char digest[SHA1_DIGEST_HEX_LENGTH + 1];
        SHA1_SNPRINTF_HEX(digest, C_ARRAY_LEN(digest), hash);
        char byte1[3] = { digest[0], digest[1], '\0' };
        char *rest = digest + 2;
        if (mkdirat(dir_fd, byte1, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "Failed to create directory: .git/objects/%s: %s\n", byte1, strerror(errno));
            return_defer(1);
        }
        fd = openat(dir_fd, byte1, O_DIRECTORY);
        if (dir_fd != AT_FDCWD) close(dir_fd);
        dir_fd = fd;

        int unlink_ret = unlinkat(dir_fd, rest, 0);
        if (unlink_ret == -1) {
            assert(errno == ENOENT);
        }
        int object_fd = openat(dir_fd, rest, O_CREAT|O_TRUNC|O_WRONLY, 0644);
        if (object_fd == -1) {
            fprintf(stderr, "Failed to create object file: .git/objects/%s/%s: %s\n", byte1, rest, strerror(errno));
            return_defer(1);
        }

        ctx.deflate.in.data = object;
        ctx.deflate.in.size = objectsize;
        if (!zlib_compress(&ctx)) {
            fprintf(stderr, "Error while zlib compressing the object\n");
            return_defer(1);
        }
        assert(write(object_fd, ctx.deflate.out.data, ctx.deflate.out.size) == (ssize_t)ctx.deflate.out.size);
    }

#undef return_defer
defer:
    if (dir_fd != AT_FDCWD) close(dir_fd);
    if (ctx.deflate.out.data) free(ctx.deflate.out.data);
    if (object) free(object);
    return ret;
}

int hash_object_command(command_t *command, const char *program, int argc, char *argv[]) {
    (void)command;
    int ret = 0;
    FILE *file = NULL;
    uint8_t *filedata = NULL;
    uint8_t *blob = NULL;
    int blob_fd = -1;
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

    uint8_t hash[SHA1_DIGEST_BYTE_LENGTH];
    if (!hash_object(BLOB, filedata, size, hash, writeblob)) {
        fprintf(stderr, "Error hashing blob for %s\n", filename);
        return_defer(1);
    }

    SHA1_PRINTF_HEX(hash);
    printf("\n");

#undef usage
#undef return_defer
defer:
    if (file != NULL) fclose(file);
    if (filedata != NULL) free(filedata);
    if (blob != NULL) free(blob);
    if (blob_fd != -1) close(blob_fd);
    return ret;
}

int ls_tree_command(command_t *command, const char *program, int argc, char *argv[]) {
    (void)command;
    int ret = 0;
    uint8_t *data = NULL;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define usage() do { \
    fprintf(stderr, "usage: %s ls-tree ([--name-only] | [--object-only]) <sha1-hash>\n", program); \
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
            if (*arg == '-') {
                usage();
                return_defer(1);
            }
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
    _Static_assert(NUM_OBJECTS == 4, "Objects have changed. May need handling here");
    if (filesize > 7 && strncmp((char *)data, "commit ", 7) == 0) {
        GIT_UNIMPLENTED("Get tree object from commit");
    } else if (filesize <= 5 || strncmp((char *)data, "tree ", 5) != 0) {
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
                    GIT_UNREACHABLE();
            }
        }
        if (!name_only) {
            SHA1_PRINTF_HEX(hash);
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

bool write_tree(int dir_fd, uint8_t hash[SHA1_DIGEST_BYTE_LENGTH]) {
    bool ret = true;
    uint8_array_t tree_data = {0};
    int n = -1;
    struct dirent **dirlist = NULL;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
    n = scandirat(dir_fd, ".", &dirlist, NULL, alphasort);
    if (n == -1) {
        return_defer(false);
    }
    for (int i = 0; i < n; i ++) {
        // FIXME handle .gitignore
        // FIXME handle staging area (i.e. .git/index)
        if (strcmp(dirlist[i]->d_name, ".") == 0 ||
                strcmp(dirlist[i]->d_name, "..") == 0 ||
                strcmp(dirlist[i]->d_name, ".git") == 0) {
            continue;
        }
        struct stat filestat = {0};
        assert(fstatat(dir_fd, dirlist[i]->d_name, &filestat, 0) == 0);
        switch (filestat.st_mode & S_IFMT) {
            case S_IFDIR: {
                int fd = openat(dir_fd, dirlist[i]->d_name, O_RDONLY);
                assert(fd > 0);
                if (!write_tree(fd, hash)) {
                    continue;
                }
                ARRAY_ENSURE(tree_data, 8 + strlen(dirlist[i]->d_name) + SHA1_DIGEST_BYTE_LENGTH);
                int written = snprintf((char *)tree_data.data + tree_data.size,
                            8 + strlen(dirlist[i]->d_name),
                            "%o %s%c",
                            GIT_DIR_MODE,
                            dirlist[i]->d_name,
                            '\0');
                assert(written > 0);
                tree_data.size += written;
                assert(tree_data.size + SHA1_DIGEST_BYTE_LENGTH <= tree_data.capacity);
                ARRAY_APPEND_BYTES(tree_data, hash, SHA1_DIGEST_BYTE_LENGTH);

            }; break;

            case S_IFREG: {
                ARRAY_ENSURE(tree_data, 8 + strlen(dirlist[i]->d_name) + SHA1_DIGEST_BYTE_LENGTH);
                int written = snprintf((char *)tree_data.data + tree_data.size,
                            8 + strlen(dirlist[i]->d_name),
                            "%o %s%c",
                            ((filestat.st_mode & S_IEXEC) != 0) ? GIT_EXE_MODE : GIT_FILE_MODE,
                            dirlist[i]->d_name,
                            '\0');
                assert(written > 0);
                tree_data.size += written;

                uint8_t *data = NULL;
                long size = 0;
                assert(file_read_contents_at(dir_fd, dirlist[i]->d_name, &data, &size));
                assert(hash_object(BLOB, data, size, hash, true));
                free(data);

                ARRAY_APPEND_BYTES(tree_data, hash, SHA1_DIGEST_BYTE_LENGTH);
            }; break;

            case S_IFBLK:
            case S_IFCHR:
            case S_IFIFO:
            case S_IFLNK:
            case S_IFSOCK:
            default:
                GIT_UNREACHABLE();
        }
    }
    hexdump(tree_data.data, tree_data.size);
    if (tree_data.size == 0) {
        return_defer(false);
    }

    if (!hash_object(TREE, tree_data.data, tree_data.size, hash, true)) {
        return_defer(false);
    }

    return_defer(true);
#undef return_defer
defer:
    if (dirlist != NULL) {
        while (n-- > 0) {
            if (dirlist[n] != NULL) free(dirlist[n]);
        }
        free(dirlist);
    }
    if (tree_data.data) free(tree_data.data);
    return ret;
}

int write_tree_command(command_t *command, const char *program, int argc, char *argv[]) {
    (void)command;
    (void)program;
    int ret = 0;
    int root_fd = AT_FDCWD;
#define return_defer(code) do { ret = (code); goto defer; } while (0);
    if (argc > 0) {
        (void)argv;
        GIT_UNIMPLENTED("args for write-tree");
        return_defer(1);
    }
    // FIXME in the future, this will parse .git/index (man gitformat-index)
    // FIXME handle .gitignore
    uint8_t hash[SHA1_DIGEST_BYTE_LENGTH];
    // FIXME work inside folders
    root_fd = openat(root_fd, ".", O_RDONLY);
    assert(root_fd > 0);
    if (!write_tree(root_fd, hash)) {
        return_defer(1);
    }

    SHA1_PRINTF_HEX(hash);
    printf("\n");

#undef return_defer
defer:
    if (root_fd != AT_FDCWD) close(root_fd);
    return ret;
}

int commit_tree_command(command_t *command, const char *program, int argc, char *argv[]) {
    int ret = 0;
    string_array_t parents = {0};
    string_array_t messages = {0};
    char *tree = NULL;
    uint8_array_t content = {0};
#define return_defer(code) do { ret = (code); goto defer; } while (0);
#define usage() do { \
    char *help_argv[] = { command->name }; \
    help_command(NULL, program, 1, help_argv); \
} while (0)

    if (argc <= 0) {
        usage();
        return_defer(1);
    }

    while (argc > 0) {
        char *arg = ARG();
        if (strcmp(arg, "-p") == 0) {
            if (argc == 0) {
                usage();
                return_defer(1);
            }
            arg = ARG();
            char *parent = strdup(arg);
            assert(parent != NULL);
            if (strlen(parent) != SHA1_DIGEST_HEX_LENGTH) {
                GIT_UNIMPLENTED("Get hash from partial hash");
            }
            ARRAY_APPEND(parents, parent);
        } else if (strcmp(arg, "-m") == 0) {
            if (argc == 0) {
                usage();
                return_defer(1);
            }
            arg = ARG();
            char *message = strdup(arg);
            assert(message != NULL);
            ARRAY_APPEND(messages, message);
        } else if (strcmp(arg, "-F") == 0) {
            if (argc == 0) {
                usage();
                return_defer(1);
            }
            char *filename = ARG();
            FILE *file = fopen(filename, "r");
            if (file == NULL) {
                fprintf(stderr, "Could not open file %s for reading\n", filename);
                return_defer(1);
            }
            uint8_t *data;
            long size;
            if (!file_read_contents_fp(file, &data, &size)) {
                fprintf(stderr, "Could read file %s\n", filename);
                fclose(file);
                return_defer(1);
            }
            ARRAY_APPEND(messages, (char *)data);
            fclose(file);
        } else {
            if (*arg == '-') {
                usage();
                return_defer(1);
            }
            // FIXME get the hash from an object name (i.e. HEAD)
            if (tree != NULL) {
                fprintf(stderr, "Can't have multiple tree's. Already have %s, and also have %s\n", tree, arg);
                return_defer(1);
            }
            tree = strdup(arg);
            assert(tree != NULL);
            if (strlen(tree) != SHA1_DIGEST_HEX_LENGTH) {
                printf("tree = %s, len = %ld\n", tree, strlen(tree));
                GIT_UNIMPLENTED("Get hash from partial hash");
            }
        }
    }

    if (tree == NULL) {
        fprintf(stderr, "No tree supplied in command line\n");
        return_defer(1);
    }

    if (messages.size == 0) {
        GIT_UNIMPLENTED("Read message from stdin");
    }

    if (parents.size > 1) {
        GIT_UNIMPLENTED("Merge requests");
    } else {
        // FIXME unhardcode this
        char *authorname = "Codecrafters test";
        char *authoremail = "hugh@davenport.net.nz";
        char *committername = authorname;
        char *committeremail = authoremail;

        time_t t = time(NULL);
        struct tm lt = {0};
        localtime_r(&t, &lt);
        int tz = 100 * lt.tm_gmtoff / 60 / 60;


        size_t size = strlen("tree ") + SHA1_DIGEST_HEX_LENGTH + 1 +
            parents.size * (strlen("parent ") + SHA1_DIGEST_HEX_LENGTH + 1) +
            strlen("author ") + strlen(authorname) + strlen(authoremail) + 21 +
            strlen("committer ") + strlen(committername) + strlen(committeremail) + 21;
        for (size_t i = 0; i < messages.size; i ++) {
            size += 1 + strlen(messages.data[i]);
        }
        size ++;
        ARRAY_ENSURE(content, size);

        char treeline[SHA1_DIGEST_HEX_LENGTH + 7];
        int length;
        if ((length = snprintf(treeline, C_ARRAY_LEN(treeline), "tree %s\n", tree)) < 0 || (unsigned)length >= C_ARRAY_LEN(treeline)) {
            fprintf(stderr, "Error writing tree line\n");
            return_defer(1);
        }
        ARRAY_APPEND_BYTES(content, treeline, length);

        for (size_t i = 0; i < parents.size; i ++) {
            char parentline[SHA1_DIGEST_HEX_LENGTH + 9];
            int length;
            if ((length = snprintf(parentline, C_ARRAY_LEN(parentline), "parent %s\n", parents.data[i])) < 0 || (unsigned)length >= C_ARRAY_LEN(parentline)) {
                fprintf(stderr, "Error writing parent line %ld\n", i);
                return_defer(1);
            }
            ARRAY_APPEND_BYTES(content, parentline, length);
        }

        char *author = NULL;
        if ((length = asprintf(&author, "author %s <%s> %ld %+d\n", authorname, authoremail, t, tz)) < 0 || author == NULL) {
            fprintf(stderr, "Error writing author line\n");
            return_defer(1);
        }
        ARRAY_APPEND_BYTES(content, author, length);
        free(author);

        char *committer = NULL;
        if ((length = asprintf(&committer, "committer %s <%s> %ld %+d\n", committername, committeremail, t, tz)) < 0 || committer == NULL) {
            fprintf(stderr, "Error writing committer line\n");
            return_defer(1);
        }
        ARRAY_APPEND_BYTES(content, committer, length);
        free(committer);

        for (size_t i = 0; i < messages.size; i ++) {
            ARRAY_APPEND(content, '\n');
            ARRAY_APPEND_BYTES(content, messages.data[i], strlen(messages.data[i]));
        }
        ARRAY_APPEND(content, '\n');

        /* assert(size == content.size); */
    }

    uint8_t hash[SHA1_DIGEST_BYTE_LENGTH];
    if (!hash_object(COMMIT, content.data, content.size, hash, true)) {
        fprintf(stderr, "Error hashing commit\n");
        return_defer(1);
    }

    SHA1_PRINTF_HEX(hash);
    printf("\n");

#undef usage
#undef return_defer
defer:
    if (content.data) free(content.data);
    if (parents.data) {
        for (size_t i = 0; i < parents.size; i ++) {
            free(parents.data[i]);
        }
        free(parents.data);
    }
    if (messages.data) {
        for (size_t i = 0; i < messages.size; i ++) {
            free(messages.data[i]);
        }
        free(messages.data);
    }
    if (tree) free(tree);
    return ret;
}

command_t commands[] = {
    // FIXME order this and do binary chop?
    //       or could hash it for O(1)
    {
        .name = "help",
        .func = help_command,
    },
    {
        .name = "init",
        .func = init_command,
    },
    {
        .name = "cat-file",
        .args = cat_file_args,
        .func = cat_file_command,
    },
    {
        .name = "hash-object",
        .func = hash_object_command,
    },
    {
        .name = "ls-tree",
        .func = ls_tree_command,
    },
    {
        .name = "write-tree",
        .func = write_tree_command,
    },
    {
        .name = "commit-tree",
        .func = commit_tree_command,
    },
};

void print_arg(command_arg_t *arg) {
    _Static_assert(NUM_ARG_TYPES == 7, "Arg type have changed. May need handling here");
    switch (arg->typ) {
        case BOOL:
            printf("%s", arg->name);
            break;

        case STRING:
            printf("%s <string>", arg->name);
            break;

        case OBJECT_TYPE:
            printf("<type>");
            break;

        case OBJECT_HASH:
            printf("<hash>");
            break;

        case CONFLICT:
            printf("(");
            for (command_arg_t *arg_p = arg->data;
                    arg_p->typ != ARG_END;
                    arg_p++) {
                if (arg_p != arg->data) printf("|");
                print_arg(arg_p);
            }
            printf(")");
            break;

        case REQUIRED_IN_ORDER:
            for (command_arg_t *arg_p = arg->data;
                    arg_p->typ != ARG_END;
                    arg_p++) {
                if (arg_p != arg->data) printf(" ");
                print_arg(arg_p);
            }
            break;

        default:
            GIT_UNREACHABLE();
            return;
    }
}

int help_command(command_t *command, const char *program, int argc, char **argv) {
    (void)command;
    if (argc < 1) {
        printf("Usage: %s <command> [<args>]\n", program);
        printf("\n");
        printf("Available commands:\n");
        for (size_t i = 0; i < C_ARRAY_LEN(commands); i ++) {
            printf("    %s\n", commands[i].name);
        }
    } else {
        const char *command = ARG();

        for (size_t i = 0; i < C_ARRAY_LEN(commands); i ++) {
            if (strcmp(command, commands[i].name) == 0) {
                if (commands[i].args) {
                    printf("Usage: %s %s [<args>]\n", program, command);
                    printf("\n");
                    printf("Available args:\n");
                    command_arg_t *arg_p = commands[i].args;
                    while (arg_p->typ != ARG_END) {
                        printf("    ");
                        print_arg(arg_p);
                        printf("\n");
                        arg_p ++;
                    }
                } else {
                    printf("Usage: %s %s [<args>]\n", program, command);
                }
                return 0;
            }
        }

        fprintf(stderr, "Unknown command %s\n", command);
    }
    return 0;
}

int main(int argc, char *argv[]) {

    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    const char *program = ARG();

    if (argc < 1) {
        help_command(NULL, program, argc, argv);
        return 1;
    }

    const char *command = ARG();

    for (size_t i = 0; i < C_ARRAY_LEN(commands); i ++) {
        if (strcmp(command, commands[i].name) == 0) {
            return commands[i].func(&commands[i], program, argc, argv);
        }
    }

    fprintf(stderr, "Unknown command %s\n", command);
    return 1;
}

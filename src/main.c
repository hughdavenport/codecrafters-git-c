#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define ARG() *argv++; argc--

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
    bool pretty = false; (void)pretty;
    if (argc <= 0) {
        // FIXME display error
        return 1;
    }
    char *arg = ARG();
    if (strcmp(arg, "-p") == 0) {
        pretty = true;
        if (argc <= 0) {
            // FIXME display error
            return 1;
        }
        arg = ARG();
    }
    // FIXME check valid hash
    if (strlen(arg) != 40) {

        // FIXME display error
        return 1;
    }

    // FIXME check in right dir
    char *object_path = malloc(55);
    if (object_path == NULL) {
        fprintf(stderr, "Ran out of memory creating object path\n");
        return 1;
    }
    char *objects_dir = ".git/objects";
    if (sprintf(object_path, "%s/xx/%38s", objects_dir, arg + 2) == -1) {
        fprintf(stderr, "Ran out of memory creating object path\n");
        return 1;
    }
    object_path[strlen(objects_dir)+1] = arg[0];
    object_path[strlen(objects_dir)+2] = arg[1];

    fprintf(stderr, "Failed to open object file %s: %s\n", object_path, strerror(errno));
    int object_fd = open(object_path, O_RDONLY);
    if (object_fd == -1) {
        fprintf(stderr, "Failed to open object file %s: %s\n", object_path, strerror(errno));
        return 1;
    }
#define BUF_SIZE 4096
    char buf[BUF_SIZE];
    while (true) {
        ssize_t bytes = read(object_fd, buf, BUF_SIZE);
        if (bytes == 0) break;
        if (bytes == -1) {
            fprintf(stderr, "IO error reading object file %s: %s\n", object_path, strerror(errno));
            return 1;
        }
        char *p = buf;
        while (bytes > 0) {
            ssize_t write_ret = write(STDOUT_FILENO, p, bytes);
            if (write_ret <= 0) {
                fprintf(stderr, "IO error reading object file %s: %s\n", object_path, strerror(errno));
                return 1;
            }
            p += write_ret;
            bytes -= write_ret;
        }
    }

    return 0;
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
    } else {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }

    return 0;
}

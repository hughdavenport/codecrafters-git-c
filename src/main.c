#include <fcntl.h>
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

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static size_t count_characters(const char *line, size_t length)
{
    if (strlen(line) != length) {
        return (size_t)-1;
    }
    size_t count = mbstowcs(NULL, line, 0);
    if (count != (size_t)-1 && length > 0 && line[length - 1] == '\n') {
        --count;
    }
    return count;
}

static int write_all(int fd, const char *data, size_t length)
{
    while (length > 0) {
        ssize_t written = write(fd, data, length);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        data += written;
        length -= (size_t)written;
    }
    return 0;
}

static int close_fd(int fd)
{
    if (close(fd) == -1) {
        perror("close");
        return -1;
    }
    return 0;
}

static pid_t start_child(size_t index, int channels[2][2],
                         const int files[2], const char *program)
{
    pid_t pid = fork();
    if (pid != 0) {
        return pid;
    }

    if (dup2(channels[index][0], STDIN_FILENO) == -1 ||
        dup2(files[index], STDOUT_FILENO) == -1) {
        perror("dup2");
        _exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < 2; ++i) {
        if (close_fd(channels[i][0]) == -1 ||
            close_fd(channels[i][1]) == -1 ||
            close_fd(files[i]) == -1) {
            _exit(EXIT_FAILURE);
        }
    }

    execl(program, program, (char *)NULL);
    perror("execl child");
    _exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    const char *child_program = argc == 2 ? argv[1] : "./child";
    int files[2] = {-1, -1};
    int channels[2][2] = {{-1, -1}, {-1, -1}};
    pid_t children[2] = {-1, -1};
    char *line = NULL;
    size_t capacity = 0;
    int result = EXIT_FAILURE;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [child-program]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        fprintf(stderr, "The C.UTF-8 locale is required.\n");
        return EXIT_FAILURE;
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < 2; ++i) {
        ssize_t length = getline(&line, &capacity, stdin);
        if (length < 0) {
            if (ferror(stdin)) {
                perror("getline filename");
            } else {
                fprintf(stderr, "Expected two output filenames.\n");
            }
            goto cleanup;
        }
        if (length > 0 && line[length - 1] == '\n') {
            line[--length] = '\0';
        }
        if (length == 0 || memchr(line, '\0', (size_t)length) != NULL) {
            fprintf(stderr, "Invalid output filename.\n");
            goto cleanup;
        }

        files[i] = open(line, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (files[i] == -1) {
            perror("open output file");
            goto cleanup;
        }
    }

    for (size_t i = 0; i < 2; ++i) {
        if (pipe(channels[i]) == -1) {
            perror("pipe");
            goto cleanup;
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        children[i] = start_child(i, channels, files, child_program);
        if (children[i] == -1) {
            perror("fork");
            goto cleanup;
        }
    }

    for (size_t i = 0; i < 2; ++i) {
        int status = close_fd(channels[i][0]);
        channels[i][0] = -1;
        if (status == -1) {
            goto cleanup;
        }
        status = close_fd(files[i]);
        files[i] = -1;
        if (status == -1) {
            goto cleanup;
        }
    }

    result = EXIT_SUCCESS;
    ssize_t length;
    while ((length = getline(&line, &capacity, stdin)) != -1) {
        size_t characters = count_characters(line, (size_t)length);
        if (characters == (size_t)-1) {
            fprintf(stderr, "Input must be UTF-8 text without NUL bytes.\n");
            result = EXIT_FAILURE;
            break;
        }
        size_t destination = characters > 10 ? 1 : 0;
        if (write_all(channels[destination][1], line, (size_t)length) == -1) {
            perror("write pipe");
            result = EXIT_FAILURE;
            break;
        }
    }
    if (ferror(stdin)) {
        perror("getline input");
        result = EXIT_FAILURE;
    }

cleanup:
    free(line);
    for (size_t i = 0; i < 2; ++i) {
        for (size_t j = 0; j < 2; ++j) {
            if (channels[i][j] != -1 && close_fd(channels[i][j]) == -1) {
                result = EXIT_FAILURE;
            }
        }
        if (files[i] != -1 && close_fd(files[i]) == -1) {
            result = EXIT_FAILURE;
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        if (children[i] <= 0) {
            continue;
        }
        int status;
        pid_t waited;
        do {
            waited = waitpid(children[i], &status, 0);
        } while (waited == -1 && errno == EINTR);

        if (waited == -1) {
            perror("waitpid");
            result = EXIT_FAILURE;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
            fprintf(stderr, "Child %zu failed.\n", i + 1);
            result = EXIT_FAILURE;
        }
    }
    return result;
}

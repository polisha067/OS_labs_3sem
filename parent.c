#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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

static int close_fd(int fd)
{
    if (close(fd) == -1) {
        perror("close");
        return -1;
    }
    return 0;
}

static int create_exchange_file(void)
{
    char name[] = "/tmp/os_lab4_XXXXXX";
    int fd = mkstemp(name);
    if (fd == -1) {
        perror("mkstemp");
        return -1;
    }
    if (unlink(name) == -1) {
        perror("unlink");
        close_fd(fd);
        return -1;
    }
    return fd;
}

static pid_t start_child(size_t index, const int exchange[2],
                         const int files[2], const char *program)
{
    pid_t pid = fork();
    if (pid != 0) {
        return pid;
    }

    if (dup2(files[index], STDOUT_FILENO) == -1) {
        perror("dup2");
        _exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < 2; ++i) {
        if (close_fd(files[i]) == -1 ||
            (i != index && close_fd(exchange[i]) == -1)) {
            _exit(EXIT_FAILURE);
        }
    }

    char descriptor[32];
    snprintf(descriptor, sizeof(descriptor), "%d", exchange[index]);
    execl(program, program, descriptor, (char *)NULL);
    perror("execl child");
    _exit(EXIT_FAILURE);
}

static int wait_for_ack(pid_t child, const sigset_t *events)
{
    for (;;) {
        siginfo_t info;
        int event = sigwaitinfo(events, &info);
        if (event == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("sigwaitinfo");
            return -1;
        }
        if (event == SIGUSR2 && info.si_pid == child) {
            return 0;
        }
        if (event == SIGCHLD) {
            fprintf(stderr, "A child exited before processing finished.\n");
            return -1;
        }
    }
}

static int send_line(int fd, pid_t child, const char *line,
                     size_t length, const sigset_t *events)
{
    size_t size = length + 1;
    if (ftruncate(fd, (off_t)size) == -1) {
        perror("ftruncate exchange file");
        return -1;
    }
    char *memory = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    memcpy(memory, line, size);

    int result = kill(child, SIGUSR1);
    if (result == -1) {
        perror("kill SIGUSR1");
    } else {
        result = wait_for_ack(child, events);
    }
    if (munmap(memory, size) == -1) {
        perror("munmap");
        result = -1;
    }
    return result;
}

int main(int argc, char *argv[])
{
    const char *child_program = argc == 2 ? argv[1] : "./child";
    int files[2] = {-1, -1};
    int exchange[2] = {-1, -1};
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

    sigset_t blocked;
    sigset_t events;
    if (sigemptyset(&blocked) == -1 ||
        sigaddset(&blocked, SIGUSR1) == -1 ||
        sigaddset(&blocked, SIGUSR2) == -1 ||
        sigaddset(&blocked, SIGCHLD) == -1 ||
        sigprocmask(SIG_BLOCK, &blocked, NULL) == -1 ||
        sigemptyset(&events) == -1 ||
        sigaddset(&events, SIGUSR2) == -1 ||
        sigaddset(&events, SIGCHLD) == -1) {
        perror("prepare signals");
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
        exchange[i] = create_exchange_file();
        if (exchange[i] == -1) {
            goto cleanup;
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        children[i] = start_child(i, exchange, files, child_program);
        if (children[i] == -1) {
            perror("fork");
            goto cleanup;
        }
    }

    for (size_t i = 0; i < 2; ++i) {
        int status = close_fd(files[i]);
        files[i] = -1;
        if (status == -1) {
            goto cleanup;
        }
    }

    result = EXIT_SUCCESS;
    for (;;) {
        errno = 0;
        ssize_t length = getline(&line, &capacity, stdin);
        if (length == -1) {
            if (!feof(stdin)) {
                perror("getline input");
                result = EXIT_FAILURE;
            }
            break;
        }
        size_t characters = count_characters(line, (size_t)length);
        if (characters == (size_t)-1) {
            fprintf(stderr, "Input must be UTF-8 text without NUL bytes.\n");
            result = EXIT_FAILURE;
            break;
        }
        size_t destination = characters > 10 ? 1 : 0;
        if (send_line(exchange[destination], children[destination],
                      line, (size_t)length, &events) == -1) {
            result = EXIT_FAILURE;
            break;
        }
    }
    if (result == EXIT_SUCCESS) {
        for (size_t i = 0; i < 2; ++i) {
            if (ftruncate(exchange[i], 0) == -1 ||
                kill(children[i], SIGUSR1) == -1) {
                perror("finish child");
                result = EXIT_FAILURE;
                break;
            }
        }
    }

cleanup:
    free(line);
    if (result != EXIT_SUCCESS) {
        for (size_t i = 0; i < 2; ++i) {
            if (children[i] > 0 && kill(children[i], SIGKILL) == -1 && errno != ESRCH) {
                perror("kill child");
            }
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        if (files[i] != -1 && close_fd(files[i]) == -1) {
            result = EXIT_FAILURE;
        }
        if (exchange[i] != -1 && close_fd(exchange[i]) == -1) {
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

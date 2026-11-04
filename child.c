#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

static int is_vowel(wchar_t character)
{
    return wcschr(L"AEIOUaeiouАЕЁИОУЫЭЮЯаеёиоуыэюя", character) != NULL;
}

static int process_line(const char *line, size_t length)
{
    while (length > 0) {
        wchar_t character;
        int bytes = mbtowc(&character, line, length);
        if (bytes <= 0) {
            fprintf(stderr, "Invalid UTF-8 text.\n");
            return -1;
        }
        if (!is_vowel(character) && fputwc(character, stdout) == WEOF) {
            perror("write output");
            return -1;
        }
        line += bytes;
        length -= (size_t)bytes;
    }
    if (fflush(stdout) == EOF) {
        perror("flush output");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s exchange-file-descriptor\n", argv[0]);
        return EXIT_FAILURE;
    }
    char *end;
    errno = 0;
    long descriptor = strtol(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' ||
        descriptor < 0 || descriptor > INT_MAX) {
        fprintf(stderr, "Invalid file descriptor.\n");
        return EXIT_FAILURE;
    }
    int fd = (int)descriptor;
    if (setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        fprintf(stderr, "The C.UTF-8 locale is required.\n");
        return EXIT_FAILURE;
    }

    sigset_t ready;
    if (sigemptyset(&ready) == -1 || sigaddset(&ready, SIGUSR1) == -1 ||
        sigprocmask(SIG_BLOCK, &ready, NULL) == -1) {
        perror("prepare signals");
        return EXIT_FAILURE;
    }
    pid_t parent = getppid();
    int result = EXIT_SUCCESS;
    for (;;) {
        siginfo_t info;
        int event = sigwaitinfo(&ready, &info);
        if (event == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("sigwaitinfo");
            result = EXIT_FAILURE;
            break;
        }
        if (info.si_pid != parent) {
            continue;
        }

        struct stat file_info;
        if (fstat(fd, &file_info) == -1) {
            perror("fstat");
            result = EXIT_FAILURE;
            break;
        }
        if (file_info.st_size == 0) {
            break;
        }
        size_t size = (size_t)file_info.st_size;
        const char *memory = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
        if (memory == MAP_FAILED) {
            perror("mmap");
            result = EXIT_FAILURE;
            break;
        }
        int processed;
        if (memory[size - 1] != '\0') {
            fprintf(stderr, "Missing end of mapped string.\n");
            processed = -1;
        } else {
            processed = process_line(memory, size - 1);
        }
        if (munmap((void *)memory, size) == -1) {
            perror("munmap");
            processed = -1;
        }
        if (processed == -1) {
            result = EXIT_FAILURE;
            break;
        }
        if (kill(parent, SIGUSR2) == -1) {
            perror("kill SIGUSR2");
            result = EXIT_FAILURE;
            break;
        }
    }
    if (close(fd) == -1) {
        perror("close exchange file");
        result = EXIT_FAILURE;
    }
    if (fclose(stdout) == EOF) {
        perror("close output");
        result = EXIT_FAILURE;
    }
    return result;
}

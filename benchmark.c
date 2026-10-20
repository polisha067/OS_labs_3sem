#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct measurement {
    double time;
    size_t threads;
    char triangle[128];
    char area[128];
};

static void print_error(const char *message, int code)
{
    fprintf(stderr, "%s. Код ошибки: %d.\n", message, code);
}

static int compare_times(const void *left, const void *right)
{
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static int make_input(FILE *input, size_t count)
{
    uint32_t state = 20;
    if (fprintf(input, "%zu\n", count) < 0) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        int coordinates[3];
        for (size_t j = 0; j < 3; ++j) {
            state = state * UINT32_C(1664525) + UINT32_C(1013904223);
            coordinates[j] = (int)(state % 2001) - 1000;
        }
        if (fprintf(input, "%d %d %d\n", coordinates[0], coordinates[1],
                    coordinates[2]) < 0) {
            return -1;
        }
    }
    return fflush(input);
}

static int run_program(FILE *input, size_t threads, struct measurement *value)
{
    FILE *output = tmpfile();
    if (output == NULL) {
        print_error("Не удалось создать временный файл для результатов", errno);
        return -1;
    }
    if (fseek(input, 0, SEEK_SET) != 0) {
        print_error("Не удалось перейти к началу файла с точками", errno);
        fclose(output);
        return -1;
    }
    char argument[32];
    snprintf(argument, sizeof(argument), "%zu", threads);
    pid_t child = fork();
    if (child == -1) {
        print_error("Не удалось создать процесс для запуска lab3", errno);
        fclose(output);
        return -1;
    }
    if (child == 0) {
        if (dup2(fileno(input), STDIN_FILENO) == -1 ||
            dup2(fileno(output), STDOUT_FILENO) == -1) {
            print_error("Не удалось подключить файлы к вводу и выводу lab3", errno);
            _exit(EXIT_FAILURE);
        }
        execl("./lab3", "./lab3", "-t", argument, (char *)NULL);
        print_error("Не удалось запустить ./lab3. Сначала выполните make lab3", errno);
        _exit(EXIT_FAILURE);
    }

    int status;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited == -1 && errno == EINTR);
    if (waited == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Не удалось получить результат: lab3 завершилась с ошибкой "
                "или не получилось дождаться её завершения.\n");
        fclose(output);
        return -1;
    }
    if (fseek(output, 0, SEEK_SET) != 0) {
        print_error("Не удалось перейти к началу файла с результатами", errno);
        fclose(output);
        return -1;
    }

    *value = (struct measurement){.time = -1.0};
    char line[256];
    while (fgets(line, sizeof(line), output) != NULL) {
        if (strncmp(line, "Процесс:", strlen("Процесс:")) == 0) {
            long pid;
            size_t count;
            size_t actual;
            if (sscanf(line, "Процесс: %ld. Точек: %zu. Потоков: %zu.", &pid, &count,
                       &actual) == 3) {
                value->threads = actual;
            }
        } else if (strncmp(line, "Самый большой треугольник образуют точки:",
                           strlen("Самый большой треугольник образуют точки:")) == 0) {
            snprintf(value->triangle, sizeof(value->triangle), "%.127s", line);
        } else if (strncmp(line, "Площадь:", strlen("Площадь:")) == 0) {
            snprintf(value->area, sizeof(value->area), "%.127s", line);
        } else if (strncmp(line, "Время поиска:", strlen("Время поиска:")) == 0) {
            if (sscanf(line, "Время поиска: %lf", &value->time) != 1) {
                value->time = -1.0;
            }
        }
    }
    int failed = ferror(output);
    if (fclose(output) == EOF) {
        failed = 1;
    }
    if (failed || !isfinite(value->time) || value->time <= 0 ||
        value->threads == 0 || value->triangle[0] == '\0' ||
        value->area[0] == '\0') {
        fprintf(stderr, "Не удалось прочитать результаты lab3 или измеренное время "
                "слишком мало. Проверьте вывод программы.\n");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    long repeats = 3;
    if (argc == 2) {
        char *end;
        errno = 0;
        repeats = strtol(argv[1], &end, 10);
        if (errno != 0 || *end != '\0' || repeats < 1 || repeats > 1000) {
            repeats = 0;
        }
    }
    if (argc > 2 || repeats == 0) {
        fprintf(stderr, "Число повторов должно быть целым числом от 1 до 1000.\n"
                "Пример запуска: %s 3\n", argv[0]);
        return EXIT_FAILURE;
    }

    double *times = malloc((size_t)repeats * sizeof(*times));
    if (times == NULL) {
        print_error("Не удалось выделить память для результатов замеров", errno);
        return EXIT_FAILURE;
    }
    const size_t sizes[] = {200, 500, 1000};
    const size_t thread_counts[] = {1, 2, 4};
    int result = EXIT_SUCCESS;
    printf("Проверяем 200, 500 и 1000 точек при 1, 2 и 4 потоках.\n"
           "Число повторов для каждого варианта: %ld. В таблице — медиана времени.\n\n",
           repeats);
    printf("Точек | Потоков | Время, с  | Ускорение | Эффективность\n");

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        FILE *input = tmpfile();
        if (input == NULL) {
            print_error("Не удалось создать временный файл с точками", errno);
            result = EXIT_FAILURE;
            break;
        }
        if (make_input(input, sizes[i]) == -1) {
            print_error("Не удалось записать точки во временный файл", errno);
            fclose(input);
            result = EXIT_FAILURE;
            break;
        }
        struct measurement reference = {0};
        double baseline = 0;
        for (size_t j = 0; j < sizeof(thread_counts) / sizeof(thread_counts[0]); ++j) {
            struct measurement value = {0};
            for (long repeat = 0; repeat < repeats; ++repeat) {
                if (run_program(input, thread_counts[j], &value) == -1) {
                    result = EXIT_FAILURE;
                    break;
                }
                if (j == 0 && repeat == 0) {
                    reference = value;
                }
                if (strcmp(value.triangle, reference.triangle) != 0 ||
                    strcmp(value.area, reference.area) != 0 ||
                    value.threads != thread_counts[j]) {
                    fprintf(stderr, "Результаты не совпали с однопоточным запуском "
                            "или программа использовала другое число потоков.\n");
                    result = EXIT_FAILURE;
                    break;
                }
                times[repeat] = value.time;
            }
            if (result != EXIT_SUCCESS) {
                break;
            }
            qsort(times, (size_t)repeats, sizeof(*times), compare_times);
            double median = times[repeats / 2];
            if (repeats % 2 == 0) {
                median = (times[repeats / 2 - 1] + median) / 2;
            }
            if (j == 0) {
                baseline = median;
            }
            double speedup = baseline / median;
            printf("%5zu | %7zu | %.6f | %9.3f | %.3f\n", sizes[i], value.threads,
                   median, speedup, speedup / (double)value.threads);
        }
        if (fclose(input) == EOF) {
            print_error("Не удалось закрыть временный файл с точками", errno);
            result = EXIT_FAILURE;
        }
        if (result != EXIT_SUCCESS) {
            break;
        }
    }
    free(times);
    if (fflush(stdout) == EOF) {
        print_error("Не удалось вывести результаты замеров", errno);
        result = EXIT_FAILURE;
    }
    return result;
}

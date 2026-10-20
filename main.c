#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct point {
    double x;
    double y;
    double z;
};

struct result {
    double square;
    size_t indices[3];
};

struct shared_data {
    struct point *points;
    size_t count;
    size_t next;
    pthread_mutex_t mutex;
};

struct worker_data {
    struct shared_data *shared;
    struct result best;
    int failed;
};

static void print_error(const char *message, int code)
{
    fprintf(stderr, "%s. Код ошибки: %d.\n", message, code);
}

static int parse_size(const char *text, size_t *value)
{
    if (text[0] < '0' || text[0] > '9') {
        return -1;
    }
    char *end;
    errno = 0;
    unsigned long long number = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || number == 0 || number > SIZE_MAX) {
        return -1;
    }
    *value = (size_t)number;
    return 0;
}

static double area_square(struct point a, struct point b, struct point c)
{
    double ab_x = b.x - a.x;
    double ab_y = b.y - a.y;
    double ab_z = b.z - a.z;
    double ac_x = c.x - a.x;
    double ac_y = c.y - a.y;
    double ac_z = c.z - a.z;

    double x = ab_y * ac_z - ab_z * ac_y;
    double y = ab_z * ac_x - ab_x * ac_z;
    double z = ab_x * ac_y - ab_y * ac_x;
    return (x * x + y * y + z * z) / 4.0;
}

static int is_better(const struct result *candidate, const struct result *best)
{
    if (candidate->square != best->square) {
        return candidate->square > best->square;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (candidate->indices[i] != best->indices[i]) {
            return candidate->indices[i] < best->indices[i];
        }
    }
    return 0;
}

static void *find_triangle(void *argument)
{
    struct worker_data *worker = argument;
    struct shared_data *shared = worker->shared;

    printf("Поток %lu начал работу (PID=%ld).\n",
           (unsigned long)pthread_self(), (long)getpid());

    while (1) {
        int error = pthread_mutex_lock(&shared->mutex);
        if (error != 0) {
            print_error("Не удалось заблокировать общий счётчик", error);
            worker->failed = 1;
            return NULL;
        }
        size_t i = shared->next;
        if (i < shared->count - 2) {
            ++shared->next;
        }
        error = pthread_mutex_unlock(&shared->mutex);
        if (error != 0) {
            print_error("Не удалось разблокировать общий счётчик", error);
            worker->failed = 1;
            return NULL;
        }
        if (i >= shared->count - 2) {
            break;
        }

        for (size_t j = i + 1; j < shared->count - 1; ++j) {
            for (size_t k = j + 1; k < shared->count; ++k) {
                struct result candidate = {
                    .square = area_square(shared->points[i], shared->points[j],
                                          shared->points[k]),
                    .indices = {i, j, k}
                };
                if (is_better(&candidate, &worker->best)) {
                    worker->best = candidate;
                }
            }
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    size_t thread_count;
    if (argc != 3 || strcmp(argv[1], "-t") != 0 ||
        parse_size(argv[2], &thread_count) == -1) {
        fprintf(stderr, "Укажите число потоков — целое число больше нуля.\n"
                "Пример запуска: %s -t 4\n", argv[0]);
        return EXIT_FAILURE;
    }

    char number[64];
    size_t count;
    printf("Введите количество точек (не меньше 3):\n");
    fflush(stdout);
    if (scanf("%63s", number) != 1 || parse_size(number, &count) == -1 ||
        count < 3 || count > SIZE_MAX / sizeof(struct point)) {
        fprintf(stderr, "Не удалось прочитать количество точек. "
                "Нужно целое число не меньше 3, которое помещается в память.\n");
        return EXIT_FAILURE;
    }

    struct point *points = malloc(count * sizeof(*points));
    if (points == NULL) {
        print_error("Не удалось выделить память для точек", errno);
        return EXIT_FAILURE;
    }
    printf("Теперь введите координаты каждой точки: x y z.\n");
    fflush(stdout);
    for (size_t i = 0; i < count; ++i) {
        if (scanf("%lf %lf %lf", &points[i].x, &points[i].y, &points[i].z) != 3 ||
            !isfinite(points[i].x) || !isfinite(points[i].y) ||
            !isfinite(points[i].z) || fabs(points[i].x) > 1e75 ||
            fabs(points[i].y) > 1e75 || fabs(points[i].z) > 1e75) {
            fprintf(stderr, "Не удалось прочитать точку %zu. "
                    "Нужны три числа: x y z.\n"
                    "Координаты должны быть конечными и по модулю "
                    "не больше 1e75. Дробную часть записывайте через точку.\n", i + 1);
            free(points);
            return EXIT_FAILURE;
        }
    }

    if (thread_count > count - 2) {
        thread_count = count - 2;
    }
    if (thread_count > SIZE_MAX / sizeof(struct worker_data) ||
        thread_count > SIZE_MAX / sizeof(pthread_t)) {
        fprintf(stderr, "Для такого количества потоков не хватит памяти. "
                "Укажите меньшее число после -t.\n");
        free(points);
        return EXIT_FAILURE;
    }

    struct worker_data *workers = calloc(thread_count, sizeof(*workers));
    pthread_t *threads = calloc(thread_count, sizeof(*threads));
    if (workers == NULL || threads == NULL) {
        print_error("Не удалось выделить память для потоков", errno);
        free(workers);
        free(threads);
        free(points);
        return EXIT_FAILURE;
    }

    struct shared_data shared = {.points = points, .count = count, .next = 0};
    int error = pthread_mutex_init(&shared.mutex, NULL);
    if (error != 0) {
        print_error("Не удалось создать мьютекс для общего счётчика", error);
        free(workers);
        free(threads);
        free(points);
        return EXIT_FAILURE;
    }

    int result = EXIT_SUCCESS;
    size_t started = 0;
    struct timespec start, finish;
    printf("Процесс: %ld. Точек: %zu. Потоков: %zu.\n",
           (long)getpid(), count, thread_count);
    fflush(stdout);

    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
        print_error("Не удалось начать замер времени", errno);
        result = EXIT_FAILURE;
        goto cleanup;
    }
    for (size_t i = 0; i < thread_count; ++i) {
        workers[i].shared = &shared;
        workers[i].best.square = -1.0;
    }
    for (size_t i = 1; i < thread_count; ++i) {
        error = pthread_create(&threads[i], NULL, find_triangle, &workers[i]);
        if (error != 0) {
            print_error("Не удалось создать рабочий поток", error);
            result = EXIT_FAILURE;
            break;
        }
        ++started;
    }
    if (result == EXIT_SUCCESS) {
        find_triangle(&workers[0]);
    }
    for (size_t i = 1; i <= started; ++i) {
        error = pthread_join(threads[i], NULL);
        if (error != 0) {
            print_error("Не удалось дождаться завершения рабочего потока", error);
            /* Работающий поток ещё может использовать общую память. */
            exit(EXIT_FAILURE);
        }
    }
    if (clock_gettime(CLOCK_MONOTONIC, &finish) == -1) {
        print_error("Не удалось закончить замер времени", errno);
        result = EXIT_FAILURE;
    }
    for (size_t i = 0; i < thread_count; ++i) {
        if (workers[i].failed) {
            result = EXIT_FAILURE;
        }
    }
    if (result == EXIT_SUCCESS) {
        struct result best = workers[0].best;
        for (size_t i = 1; i < thread_count; ++i) {
            if (is_better(&workers[i].best, &best)) {
                best = workers[i].best;
            }
        }
        printf("Самый большой треугольник образуют точки:");
        for (size_t i = 0; i < 3; ++i) {
            printf(" %zu", best.indices[i] + 1);
        }
        putchar('\n');
        for (size_t i = 0; i < 3; ++i) {
            struct point point = points[best.indices[i]];
            printf("Точка %zu: (%.10g, %.10g, %.10g)\n", best.indices[i] + 1,
                   point.x, point.y, point.z);
        }
        double elapsed = (double)(finish.tv_sec - start.tv_sec) +
                         (double)(finish.tv_nsec - start.tv_nsec) / 1e9;
        printf("Площадь: %.10g\nВремя поиска: %.9f с\n", sqrt(best.square), elapsed);
    }

cleanup:
    error = pthread_mutex_destroy(&shared.mutex);
    if (error != 0) {
        print_error("Не удалось освободить мьютекс общего счётчика", error);
        result = EXIT_FAILURE;
    }
    free(threads);
    free(workers);
    free(points);
    return result;
}

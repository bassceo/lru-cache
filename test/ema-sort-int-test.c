#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>

// ----------------------------------------------------------------------
// Определения типов для функций из liblab2.so
// ----------------------------------------------------------------------
typedef int     (*lab2_open_t)(const char *);
typedef int     (*lab2_close_t)(int);
typedef ssize_t (*lab2_read_t)(int, void *, size_t);
typedef ssize_t (*lab2_write_t)(int, const void *, size_t);
typedef off_t   (*lab2_lseek_t)(int, off_t, int);
typedef int     (*lab2_fsync_t)(int);

// ----------------------------------------------------------------------
// Глобальные указатели на функции для случая "lab2", а также флажок режима
// ----------------------------------------------------------------------
static lab2_open_t  g_lab2_open  = NULL;
static lab2_close_t g_lab2_close = NULL;
static lab2_read_t  g_lab2_read  = NULL;
static lab2_write_t g_lab2_write = NULL;
static lab2_lseek_t g_lab2_lseek = NULL;
static lab2_fsync_t g_lab2_fsync = NULL;

typedef enum {
    MODE_NONE,
    MODE_SYS,  // Системные вызовы
    MODE_LIB   // Функции lab2_*
} IO_Mode;

static IO_Mode g_io_mode = MODE_NONE;

// ----------------------------------------------------------------------
// "Обёртки" для операций ввода-вывода.
// В зависимости от режима (sys или lib) вызываем либо системный вызов,
// либо функцию из динамической библиотеки.
// ----------------------------------------------------------------------

int my_open(const char* path, int flags, mode_t mode) {
    if (g_io_mode == MODE_SYS) {
        return open(path, flags, mode);
    } else {
        // Для упрощения примера игнорируем флаги, кроме O_CREAT | O_RDWR
        // (В самой liblab2 это тоже игнорируется – там жёстко O_CREAT|O_RDWR|O_DIRECT)
        return g_lab2_open ? g_lab2_open(path) : -1;
    }
}

int my_close(int fd) {
    if (g_io_mode == MODE_SYS) {
        return close(fd);
    } else {
        return g_lab2_close ? g_lab2_close(fd) : -1;
    }
}

ssize_t my_read(int fd, void* buf, size_t count) {
    if (g_io_mode == MODE_SYS) {
        return read(fd, buf, count);
    } else {
        return g_lab2_read ? g_lab2_read(fd, buf, count) : -1;
    }
}

ssize_t my_write(int fd, const void* buf, size_t count) {
    if (g_io_mode == MODE_SYS) {
        return write(fd, buf, count);
    } else {
        return g_lab2_write ? g_lab2_write(fd, buf, count) : -1;
    }
}

off_t my_lseek(int fd, off_t offset, int whence) {
    if (g_io_mode == MODE_SYS) {
        return lseek(fd, offset, whence);
    } else {
        return g_lab2_lseek ? g_lab2_lseek(fd, offset, whence) : (off_t)-1;
    }
}

int my_fsync(int fd) {
    if (g_io_mode == MODE_SYS) {
        return fsync(fd);
    } else {
        return g_lab2_fsync ? g_lab2_fsync(fd) : -1;
    }
}

// ----------------------------------------------------------------------
// Функция для замера времени (в миллисекундах)
// ----------------------------------------------------------------------
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// ----------------------------------------------------------------------
// Генерация файла со случайными int
// ----------------------------------------------------------------------
static int generate_random_file(const char* path, long count) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        perror("fopen");
        return -1;
    }
    srand(12345); // Для воспроизводимости
    for (long i = 0; i < count; i++) {
        int val = rand();
        fwrite(&val, sizeof(int), 1, f);
    }
    fclose(f);
    return 0;
}

// ----------------------------------------------------------------------
// Сортировка массива int (в памяти) - просто qsort
// ----------------------------------------------------------------------
static int cmp_int(const void* a, const void* b) {
    int x = *(const int*)a;
    int y = *(const int*)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

// ----------------------------------------------------------------------
// Прочитать из fd до `max_count` int, записать в буфер buf. Вернуть,
// сколько реально прочли. Возвращаем -1 при ошибке.
// ----------------------------------------------------------------------
static ssize_t read_ints(int fd, int* buf, size_t max_count) {
    size_t total_read = 0;
    while (total_read < max_count) {
        ssize_t r = my_read(fd, buf + total_read, (max_count - total_read)*sizeof(int));
        if (r < 0) {
            perror("my_read");
            return -1;
        }
        if (r == 0) {
            // EOF
            break;
        }
        total_read += (r / sizeof(int));
    }
    return (ssize_t)total_read;
}

// ----------------------------------------------------------------------
// Записать в fd все int из буфера buf
// ----------------------------------------------------------------------
static int write_ints(int fd, const int* buf, size_t count) {
    size_t total_written = 0;
    while (total_written < count) {
        ssize_t w = my_write(fd, buf + total_written, (count - total_written)*sizeof(int));
        if (w < 0) {
            perror("my_write");
            return -1;
        }
        total_written += (w / sizeof(int));
    }
    return 0;
}

// ----------------------------------------------------------------------
// Фаза 1: Формирование ран. Читаем из входного файла чанками по chunk_size,
// сортируем в памяти, пишем в отдельные временные файлы (ran0.bin, ran1.bin, ...).
// Возвращаем количество созданных ран или -1 при ошибке.
// ----------------------------------------------------------------------
static int create_runs(const char* input_path, long count, long chunk_size) {
    int fd_in = my_open(input_path, O_RDONLY, 0666);
    if (fd_in < 0) {
        perror("my_open (input)");
        return -1;
    }

    long nums_left = count;
    int run_idx = 0;
    int* buffer = (int*)malloc(chunk_size * sizeof(int));
    if (!buffer) {
        fprintf(stderr, "malloc error\n");
        my_close(fd_in);
        return -1;
    }

    while (nums_left > 0) {
        char run_name[256];
        snprintf(run_name, sizeof(run_name), "run_%d.bin", run_idx);
        int fd_out = my_open(run_name, O_CREAT | O_RDWR, 0666);
        if (fd_out < 0) {
            perror("my_open (run)");
            free(buffer);
            my_close(fd_in);
            return -1;
        }
        // Считываем часть
        long to_read = (nums_left < chunk_size) ? nums_left : chunk_size;
        ssize_t got = read_ints(fd_in, buffer, to_read);
        if (got < 0) {
            my_close(fd_out);
            free(buffer);
            my_close(fd_in);
            return -1;
        }
        // Сортируем
        qsort(buffer, got, sizeof(int), cmp_int);
        // Записываем
        if (write_ints(fd_out, buffer, got) < 0) {
            my_close(fd_out);
            free(buffer);
            my_close(fd_in);
            return -1;
        }
        my_close(fd_out);

        nums_left -= got;
        run_idx++;
    }

    free(buffer);
    my_close(fd_in);
    return run_idx;
}

// ----------------------------------------------------------------------
// Маленькая структура для k-путевого слияния
// ----------------------------------------------------------------------
typedef struct {
    int fd;
    int current;   // текущий элемент из блока
    int has_value; // 1, если в current что-то есть
} RunSource;

// ----------------------------------------------------------------------
// Считываем следующий int из fd в source->current. 
// Если достигнут EOF, ставим has_value=0. Иначе has_value=1.
// ----------------------------------------------------------------------
static int read_next(RunSource* src) {
    int val;
    ssize_t r = my_read(src->fd, &val, sizeof(int));
    if (r == 0) {
        // EOF
        src->has_value = 0;
        return 0;
    } else if (r < 0) {
        perror("my_read in merge");
        return -1;
    }
    src->current = val;
    src->has_value = 1;
    return 0;
}

// ----------------------------------------------------------------------
// Фаза 2: k-путевое слияние всех ран (run_0.bin, run_1.bin, ... , run_{n-1}.bin)
// в итоговый файл output_sorted.bin
// ----------------------------------------------------------------------
static int merge_runs(int run_count, const char* output_path) {
    // Откроем итоговый файл:
    int fd_out = my_open(output_path, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd_out < 0) {
        perror("my_open (output)");
        return -1;
    }

    // Открываем все ран-файлы
    RunSource* sources = (RunSource*)calloc(run_count, sizeof(RunSource));
    if (!sources) {
        fprintf(stderr, "calloc error\n");
        my_close(fd_out);
        return -1;
    }

    for (int i = 0; i < run_count; i++) {
        char run_name[256];
        snprintf(run_name, sizeof(run_name), "run_%d.bin", i);
        sources[i].fd = my_open(run_name, O_RDONLY, 0666);
        if (sources[i].fd < 0) {
            perror("my_open (merge run)");
            // закроем уже открытые
            for (int j = 0; j < i; j++) {
                my_close(sources[j].fd);
            }
            free(sources);
            my_close(fd_out);
            return -1;
        }
    }

    // Считаем по 1 int из каждого рана в буфер
    for (int i = 0; i < run_count; i++) {
        if (read_next(&sources[i]) < 0) {
            // Ошибка
            for (int j = 0; j < run_count; j++) {
                my_close(sources[j].fd);
            }
            free(sources);
            my_close(fd_out);
            return -1;
        }
    }

    // Пока хоть один источник не пуст
    while (1) {
        int min_val = 0;
        int min_idx = -1;
        for (int i = 0; i < run_count; i++) {
            if (sources[i].has_value) {
                if (min_idx < 0 || sources[i].current < min_val) {
                    min_val = sources[i].current;
                    min_idx = i;
                }
            }
        }
        if (min_idx < 0) {
            // Все пустые
            break;
        }
        // Запишем min_val
        if (my_write(fd_out, &min_val, sizeof(int)) < 0) {
            perror("my_write (merge)");
            // освобождаем всё
            for (int j = 0; j < run_count; j++) {
                my_close(sources[j].fd);
            }
            free(sources);
            my_close(fd_out);
            return -1;
        }
        // Считаем следующий int из источника min_idx
        if (read_next(&sources[min_idx]) < 0) {
            // освобождаем всё
            for (int j = 0; j < run_count; j++) {
                my_close(sources[j].fd);
            }
            free(sources);
            my_close(fd_out);
            return -1;
        }
    }

    // Закрываем ран-файлы
    for (int i = 0; i < run_count; i++) {
        my_close(sources[i].fd);
    }
    free(sources);

    // Синхронизировать на диск и закрыть
    my_fsync(fd_out);
    my_close(fd_out);

    return 0;
}

// ----------------------------------------------------------------------
// Собственно внешняя сортировка:
//   1) Фаза создания ран
//   2) Фаза слияния
// ----------------------------------------------------------------------
static int external_sort(const char* input_path, const char* output_path,
                         long count, long chunk_size) 
{
    // 1) Формируем раны
    int run_count = create_runs(input_path, count, chunk_size);
    if (run_count < 0) {
        return -1;
    }
    if (run_count == 0) {
        // Пустой файл?
        return 0;
    }
    // 2) Сливаем раны
    if (merge_runs(run_count, output_path) < 0) {
        return -1;
    }
    return 0;
}

// ----------------------------------------------------------------------
// Проверка, что файл отсортирован
// ----------------------------------------------------------------------
static int check_sorted(const char* path, long count) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        perror("fopen check_sorted");
        return -1;
    }
    int prev = 0, cur = 0;
    if (count > 0) {
        if (fread(&prev, sizeof(int), 1, f) != 1) {
            fclose(f);
            return -1;
        }
    }
    for (long i = 1; i < count; i++) {
        if (fread(&cur, sizeof(int), 1, f) != 1) {
            fclose(f);
            return -1;
        }
        if (cur < prev) {
            fprintf(stderr, "File is NOT sorted (pos=%ld)\n", i);
            fclose(f);
            return -1;
        }
        prev = cur;
    }
    fclose(f);
    return 0;
}

// ----------------------------------------------------------------------
// Точка входа
// ----------------------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, 
            "Usage:\n"
            "  %s gen <path> <count>\n"
            "  %s sys <path> <count> <chunk>\n"
            "  %s lib <path> <count> <chunk>\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    // Если команда "gen" - просто генерируем случайный файл
    if (strcmp(argv[1], "gen") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s gen <path> <count>\n", argv[0]);
            return 1;
        }
        const char* path = argv[2];
        long count = atol(argv[3]);
        if (count <= 0) {
            fprintf(stderr, "count must be positive\n");
            return 1;
        }
        if (generate_random_file(path, count) < 0) {
            fprintf(stderr, "Error generating file\n");
            return 1;
        }
        printf("Generated %ld integers in file %s\n", count, path);
        return 0;
    }

    // Иначе предполагается режим "sys" или "lib" + сортировка
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <sys|lib> <path> <count> <chunk>\n", argv[0]);
        return 1;
    }
    const char* mode = argv[1];
    const char* path = argv[2];
    long count = atol(argv[3]);
    long chunk_size = atol(argv[4]);
    if (count <= 0 || chunk_size <= 0) {
        fprintf(stderr, "count and chunk must be positive\n");
        return 1;
    }

    if (strcmp(mode, "sys") == 0) {
        g_io_mode = MODE_SYS;
    } else if (strcmp(mode, "lib") == 0) {
        g_io_mode = MODE_LIB;
        // Загружаем библиотеку
        void *handle = dlopen("./liblab2.so", RTLD_LAZY);
        if (!handle) {
            fprintf(stderr, "Cannot open liblab2.so: %s\n", dlerror());
            return 1;
        }
        g_lab2_open  = (lab2_open_t)dlsym(handle, "lab2_open");
        g_lab2_close = (lab2_close_t)dlsym(handle, "lab2_close");
        g_lab2_read  = (lab2_read_t)dlsym(handle, "lab2_read");
        g_lab2_write = (lab2_write_t)dlsym(handle, "lab2_write");
        g_lab2_lseek = (lab2_lseek_t)dlsym(handle, "lab2_lseek");
        g_lab2_fsync = (lab2_fsync_t)dlsym(handle, "lab2_fsync");

        char *error;
        if ((error = dlerror()) != NULL) {
            fprintf(stderr, "dlsym error: %s\n", error);
            dlclose(handle);
            return 1;
        }
    } else {
        fprintf(stderr, "Unknown mode '%s'. Use sys or lib.\n", mode);
        return 1;
    }

    // Выполним внешнюю сортировку
    // Результат положим в "sorted.bin"
    const char* output_sorted = "sorted.bin";

    double t1 = get_time_ms();
    if (external_sort(path, output_sorted, count, chunk_size) < 0) {
        fprintf(stderr, "external_sort failed\n");
        return 1;
    }
    double t2 = get_time_ms();
    double dt = t2 - t1;

    // Проверим, что итоговый файл действительно отсортирован
    if (check_sorted(output_sorted, count) == 0) {
        printf("Mode=%s, external sort OK. Elapsed=%.2f ms\n", mode, dt);
    } else {
        printf("Mode=%s, external sort - file not sorted!\n", mode);
    }

    // Для чистоты эксперимента можно удалить временные ран-файлы:
    // run_0.bin, run_1.bin, ...
    // (Если очень много ран, это неэффективно делать простым циклом, но для демо - пойдёт)
    for (int i = 0; ; i++) {
        char run_name[256];
        snprintf(run_name, sizeof(run_name), "run_%d.bin", i);
        // попробуем открыть на чтение
        int fd_test = open(run_name, O_RDONLY);
        if (fd_test < 0) {
            break; // предполагаем, что таких файлов больше нет
        }
        close(fd_test);
        unlink(run_name);
    }

    return 0;
}

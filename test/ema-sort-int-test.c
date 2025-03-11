#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>

static int sys_open(const char* path, int flags, mode_t mode) {
    return open(path, flags, mode);
}

static ssize_t sys_read(int fd, void* buf, size_t count) {
    return read(fd, buf, count);
}

static ssize_t sys_write(int fd, const void* buf, size_t count) {
    return write(fd, buf, count);
}

static off_t sys_lseek(int fd, off_t offset, int whence) {
    return lseek(fd, offset, whence);
}

static int sys_close(int fd) {
    return close(fd);
}

static int sys_fsync(int fd) {
    return fsync(fd);
}

typedef int     (*lab2_open_t) (const char *);
typedef int     (*lab2_close_t)(int);
typedef ssize_t (*lab2_read_t) (int, void *, size_t);
typedef ssize_t (*lab2_write_t)(int, const void *, size_t);
typedef off_t   (*lab2_lseek_t)(int, off_t, int);
typedef int     (*lab2_fsync_t)(int);

typedef struct MyIO {
    int     (*my_open2) (const char* path, int flags, mode_t mode);
    ssize_t (*my_read2) (int, void*, size_t);
    ssize_t (*my_write2)(int, const void*, size_t);
    off_t   (*my_lseek2)(int, off_t, int);
    int     (*my_close2)(int);
    int     (*my_fsync2)(int);
} MyIO;

static MyIO g_sys_io;
static MyIO g_lab2_io;

static ssize_t read_ints(const MyIO* io, int fd, int* buf, size_t n) {
    size_t to_read = n * sizeof(int);
    size_t done = 0;
    while (done < to_read) {
        ssize_t r = io->my_read2(fd, (char*)buf + done, to_read - done);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("read");
            return -1;
        }
        if (r == 0) { // EOF
            break;
        }
        done += r;
    }
    return (done / sizeof(int));
}

static ssize_t write_ints(const MyIO* io, int fd, const int* buf, size_t n) {
    size_t to_write = n * sizeof(int);
    size_t done = 0;
    while (done < to_write) {
        ssize_t w = io->my_write2(fd, (char*)buf + done, to_write - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            perror("write");
            return -1;
        }
        if (w == 0) {
            break;
        }
        done += w;
    }
    return (done / sizeof(int));
}

static int cmp_ints(const void* a, const void* b) {
    int x = *(const int*)a;
    int y = *(const int*)b;
    return (x > y) - (x < y);
}

static int create_runs(const MyIO* io,
                       const char* input_file,
                       size_t total_ints,
                       size_t chunk_size,
                       char*** out_run_files,
                       int* out_run_count)
{
    int fd_in = io->my_open2(input_file, O_RDONLY, 0);
    if (fd_in < 0) {
        perror("open input_file");
        return -1;
    }

    int max_runs = (total_ints + chunk_size - 1) / chunk_size;
    char** runs = (char**)calloc(max_runs, sizeof(char*));
    if (!runs) {
        fprintf(stderr, "calloc runs failed\n");
        io->my_close2(fd_in);
        return -1;
    }

    int* buffer = (int*)malloc(chunk_size * sizeof(int));
    if (!buffer) {
        fprintf(stderr, "malloc buffer failed\n");
        free(runs);
        io->my_close2(fd_in);
        return -1;
    }

    int run_count = 0;
    size_t read_so_far = 0;

    while (read_so_far < total_ints) {
        size_t left = total_ints - read_so_far;
        size_t this_chunk = (left < chunk_size) ? left : chunk_size;

        ssize_t got = read_ints(io, fd_in, buffer, this_chunk);
        if (got < 0) {
            free(buffer);
            free(runs);
            io->my_close2(fd_in);
            return -1;
        }

        qsort(buffer, got, sizeof(int), cmp_ints);

        char run_name[64];
        snprintf(run_name, sizeof(run_name),
                 "run-%d-%p.bin", run_count, (void*)io);
        runs[run_count] = strdup(run_name);

        int fd_run = io->my_open2(runs[run_count],
                                  O_CREAT|O_RDWR|O_TRUNC,
                                  0666);
        if (fd_run < 0) {
            perror("open run file");
            free(buffer);
            for (int k = 0; k < run_count; k++) {
                free(runs[k]);
            }
            free(runs);
            io->my_close2(fd_in);
            return -1;
        }
        if (write_ints(io, fd_run, buffer, got) < 0) {
            io->my_close2(fd_run);
            free(buffer);
            for (int k = 0; k < run_count; k++) {
                free(runs[k]);
            }
            free(runs);
            io->my_close2(fd_in);
            return -1;
        }
        io->my_close2(fd_run);

        run_count++;
        read_so_far += got;
    }

    free(buffer);
    io->my_close2(fd_in);

    *out_run_files = runs;
    *out_run_count = run_count;
    return 0;
}

static int merge_two_runs(const MyIO* io,
                          const char* runA,
                          const char* runB,
                          const char* outFile)
{
    int fdA = io->my_open2(runA, O_RDONLY, 0);
    if (fdA < 0) {
        perror("open runA");
        return -1;
    }
    int fdB = io->my_open2(runB, O_RDONLY, 0);
    if (fdB < 0) {
        perror("open runB");
        io->my_close2(fdA);
        return -1;
    }
    int fdOut = io->my_open2(outFile, O_CREAT|O_RDWR|O_TRUNC, 0666);
    if (fdOut < 0) {
        perror("open outFile");
        io->my_close2(fdA);
        io->my_close2(fdB);
        return -1;
    }

    int valA, valB;
    ssize_t rA = read_ints(io, fdA, &valA, 1);
    ssize_t rB = read_ints(io, fdB, &valB, 1);

    while (rA > 0 && rB > 0) {
        if (valA <= valB) {
            if (write_ints(io, fdOut, &valA, 1) < 0) {
                io->my_close2(fdA);
                io->my_close2(fdB);
                io->my_close2(fdOut);
                return -1;
            }
            rA = read_ints(io, fdA, &valA, 1);
        } else {
            if (write_ints(io, fdOut, &valB, 1) < 0) {
                io->my_close2(fdA);
                io->my_close2(fdB);
                io->my_close2(fdOut);
                return -1;
            }
            rB = read_ints(io, fdB, &valB, 1);
        }
    }
    while (rA > 0) {
        if (write_ints(io, fdOut, &valA, 1) < 0) {
            io->my_close2(fdA);
            io->my_close2(fdB);
            io->my_close2(fdOut);
            return -1;
        }
        rA = read_ints(io, fdA, &valA, 1);
    }
    while (rB > 0) {
        if (write_ints(io, fdOut, &valB, 1) < 0) {
            io->my_close2(fdA);
            io->my_close2(fdB);
            io->my_close2(fdOut);
            return -1;
        }
        rB = read_ints(io, fdB, &valB, 1);
    }

    io->my_close2(fdA);
    io->my_close2(fdB);
    io->my_close2(fdOut);
    return 0;
}

static int merge_all_runs(const MyIO* io,
                          char** run_files,
                          int run_count,
                          const char* output_file)
{
    if (run_count == 1) {
        return merge_two_runs(io, run_files[0], "/dev/null", output_file);
    }

    int current_count = run_count;

    while (current_count > 1) {
        char** next_runs = (char**)calloc(current_count, sizeof(char*));
        if (!next_runs) {
            fprintf(stderr, "calloc next_runs failed\n");
            return -1;
        }
        int new_count = 0;

        for (int i = 0; i < current_count; i += 2) {
            if (i + 1 < current_count) {
                char tmp_name[64];
                snprintf(tmp_name, sizeof(tmp_name),
                         "merge-%d-%p.bin", i/2, (void*)io);

                if (merge_two_runs(io, run_files[i], run_files[i+1], tmp_name) < 0) {
                    free(next_runs);
                    return -1;
                }
                if (run_files[i]) {
                    free(run_files[i]);
                    run_files[i] = NULL;
                }
                if (run_files[i+1]) {
                    free(run_files[i+1]);
                    run_files[i+1] = NULL;
                }
                next_runs[new_count] = strdup(tmp_name);
                new_count++;
            } else {
                next_runs[new_count] = run_files[i];
                run_files[i] = NULL;
                new_count++;
            }
        }

        for (int k = 0; k < new_count; k++) {
            run_files[k] = next_runs[k];
        }
        for (int k = new_count; k < current_count; k++) {
            run_files[k] = NULL;
        }
        current_count = new_count;

        free(next_runs);
    }

    if (current_count == 1) {
        if (merge_two_runs(io, run_files[0], "/dev/null", output_file) < 0) {
            return -1;
        }
    }

    return 0;
}

static int external_sort(const MyIO* io,
                         const char* input_file,
                         const char* output_file,
                         size_t total_ints,
                         size_t chunk_size)
{
    char** run_files = NULL;
    int run_count = 0;
    if (create_runs(io, input_file, total_ints, chunk_size,
                    &run_files, &run_count) < 0)
    {
        return -1;
    }

    if (merge_all_runs(io, run_files, run_count, output_file) < 0) {
        for (int i = 0; i < run_count; i++) {
            if (run_files[i]) {
                unlink(run_files[i]);
                free(run_files[i]);
            }
        }
        free(run_files);
        return -1;
    }

    for (int i = 0; i < run_count; i++) {
        if (run_files[i]) {
            unlink(run_files[i]);
            free(run_files[i]);
        }
    }
    free(run_files);

    int fd_out = io->my_open2(output_file, O_RDWR, 0);
    if (fd_out >= 0) {
        io->my_fsync2(fd_out);
        io->my_close2(fd_out);
    }

    return 0;
}

static int generate_input_file(const MyIO* io,
                               const char* fname,
                               size_t n)
{
    int fd = io->my_open2(fname, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) {
        perror("open for generate");
        return -1;
    }
    int* data = (int*)malloc(n * sizeof(int));
    if (!data) {
        io->my_close2(fd);
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        data[i] = rand();
    }
    if (write_ints(io, fd, data, n) < 0) {
        free(data);
        io->my_close2(fd);
        return -1;
    }
    free(data);
    io->my_close2(fd);
    return 0;
}

static double measure_sort_time(const MyIO* io,
                                const char* input_file,
                                const char* output_file,
                                size_t total_ints,
                                size_t chunk_size)
{
    struct timeval t1, t2;
    gettimeofday(&t1, NULL);

    if (external_sort(io, input_file, output_file, total_ints, chunk_size) < 0) {
        return -1.0;
    }

    gettimeofday(&t2, NULL);
    double ms = (t2.tv_sec - t1.tv_sec)*1000.0 +
                (t2.tv_usec - t1.tv_usec)/1000.0;
    return ms;
}

static int sys_open_wrapper(const char* path, int flags, mode_t mode) {
    return sys_open(path, flags, mode);
}
static ssize_t sys_read_wrapper(int fd, void* buf, size_t sz) {
    return sys_read(fd, buf, sz);
}
static ssize_t sys_write_wrapper(int fd, const void* buf, size_t sz) {
    return sys_write(fd, buf, sz);
}
static off_t sys_lseek_wrapper(int fd, off_t off, int wh) {
    return sys_lseek(fd, off, wh);
}
static int sys_close_wrapper(int fd) {
    return sys_close(fd);
}
static int sys_fsync_wrapper(int fd) {
    return sys_fsync(fd);
}

static lab2_open_t   f_open   = NULL;
static lab2_close_t  f_close  = NULL;
static lab2_read_t   f_read   = NULL;
static lab2_write_t  f_write  = NULL;
static lab2_lseek_t  f_lseek  = NULL;
static lab2_fsync_t  f_fsync  = NULL;

static int lab2_open_wrapper(const char* path, int flags, mode_t mode) {
    (void)flags; // игнорируем
    (void)mode;  
    if (!f_open) return -1;
    return f_open(path);
}
static int lab2_close_wrapper(int fd) {
    if (!f_close) return -1;
    return f_close(fd);
}
static ssize_t lab2_read_wrapper(int fd, void* buf, size_t sz) {
    if (!f_read) return -1;
    return f_read(fd, buf, sz);
}
static ssize_t lab2_write_wrapper(int fd, const void* buf, size_t sz) {
    if (!f_write) return -1;
    return f_write(fd, buf, sz);
}
static off_t lab2_lseek_wrapper(int fd, off_t off, int wh) {
    if (!f_lseek) return -1;
    return f_lseek(fd, off, wh);
}
static int lab2_fsync_wrapper(int fd) {
    if (!f_fsync) return -1;
    return f_fsync(fd);
}

static int init_io_structs(void)
{
    g_sys_io.my_open2  = sys_open_wrapper;
    g_sys_io.my_read2  = sys_read_wrapper;
    g_sys_io.my_write2 = sys_write_wrapper;
    g_sys_io.my_lseek2 = sys_lseek_wrapper;
    g_sys_io.my_close2 = sys_close_wrapper;
    g_sys_io.my_fsync2 = sys_fsync_wrapper;

    void* handle = dlopen("./liblab2.so", RTLD_LAZY);
    if (!handle) {
        return -1;
    }

    *(void **)(&f_open)  = dlsym(handle, "lab2_open");
    *(void **)(&f_close) = dlsym(handle, "lab2_close");
    *(void **)(&f_read)  = dlsym(handle, "lab2_read");
    *(void **)(&f_write) = dlsym(handle, "lab2_write");
    *(void **)(&f_lseek) = dlsym(handle, "lab2_lseek");
    *(void **)(&f_fsync) = dlsym(handle, "lab2_fsync");

    char* err = dlerror();
    if (err) {
        fprintf(stderr, "dlsym error: %s\n", err);
        return -1;
    }

    g_lab2_io.my_open2  = lab2_open_wrapper;
    g_lab2_io.my_close2 = lab2_close_wrapper;
    g_lab2_io.my_read2  = lab2_read_wrapper;
    g_lab2_io.my_write2 = lab2_write_wrapper;
    g_lab2_io.my_lseek2 = lab2_lseek_wrapper;
    g_lab2_io.my_fsync2 = lab2_fsync_wrapper;

    return 0;
}

int main(void)
{
    srand((unsigned)time(NULL));

    int lab2_ok = (init_io_structs() == 0);

    struct {
        size_t total_ints;
        size_t chunk_size;
    } tests[] = {
        { 20000,  2000 },
        { 50000,  5000 },
        {100000, 10000},
    };
    int num_tests = sizeof(tests) / sizeof(tests[0]);

    printf(" total_ints | chunk_size |   sys_time(ms)  |  lab2_time(ms)\n");
    printf("------------+------------+-----------------+----------------\n");

    for (int i = 0; i < num_tests; i++) {
        size_t total = tests[i].total_ints;
        size_t chunk = tests[i].chunk_size;

        if (generate_input_file(&g_sys_io, "input.bin", total) < 0) {
            fprintf(stderr, "Failed to generate input.bin\n");
            continue;
        }

        double sys_time = measure_sort_time(&g_sys_io,
                                            "input.bin",
                                            "output_sys.bin",
                                            total,
                                            chunk);

        double lab2_time = -1.0;
        if (lab2_ok) {
            if (generate_input_file(&g_sys_io, "input.bin", total) == 0) {
                lab2_time = measure_sort_time(&g_lab2_io,
                                              "input.bin",
                                              "output_lab2.bin",
                                              total,
                                              chunk);
            }
        }

        if (sys_time < 0) {
            printf(" %10zu | %10zu |      error     |", total, chunk);
        } else {
            printf(" %10zu | %10zu | %15.2f |", total, chunk, sys_time);
        }

        if (!lab2_ok) {
            printf("   (no liblab2)\n");
        } else if (lab2_time < 0) {
            printf("      error\n");
        } else {
            printf(" %14.2f\n", lab2_time);
        }
    }

    return 0;
}

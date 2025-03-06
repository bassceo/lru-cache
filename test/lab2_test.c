#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

typedef int     (*lab2_open_t)(const char *);
typedef int     (*lab2_close_t)(int);
typedef ssize_t (*lab2_read_t)(int, void *, size_t);
typedef ssize_t (*lab2_write_t)(int, const void *, size_t);
typedef off_t   (*lab2_lseek_t)(int, off_t, int);
typedef int     (*lab2_fsync_t)(int);

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <any_mode_name> <path> <size>\n", argv[0]);
        return 1;
    }

    void *handle = dlopen("./liblab2.so", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Cannot open library: %s\n", dlerror());
        return 1;
    }

    lab2_open_t  lab2_open  = (lab2_open_t)dlsym(handle, "lab2_open");
    lab2_close_t lab2_close = (lab2_close_t)dlsym(handle, "lab2_close");
    lab2_read_t  lab2_read  = (lab2_read_t)dlsym(handle, "lab2_read");

    char *error;
    if ((error = dlerror()) != NULL) {
        fprintf(stderr, "Error dlsym: %s\n", error);
        dlclose(handle);
        return 1;
    }

    char *path = argv[2];
    long size = atol(argv[3]);
    char *buf = malloc(size);
    if (!buf) {
        perror("malloc");
        dlclose(handle);
        return 1;
    }

    int fd_sys = open(path, O_RDONLY);
    if (fd_sys < 0) {
        perror("open");
        free(buf);
        dlclose(handle);
        return 1;
    }
    struct timeval t1, t2;
    gettimeofday(&t1, NULL);
    read(fd_sys, buf, size);
    close(fd_sys);
    gettimeofday(&t2, NULL);
    double d1 = (t2.tv_sec - t1.tv_sec) * 1000.0 +
                (t2.tv_usec - t1.tv_usec) / 1000.0;

    memset(buf, 0, size);

    int fd = lab2_open(path);
    if (fd < 0) {
        fprintf(stderr, "lab2_open failed\n");
        free(buf);
        dlclose(handle);
        return 1;
    }
    gettimeofday(&t1, NULL);
    lab2_read(fd, buf, size);
    gettimeofday(&t2, NULL);
    double d2 = (t2.tv_sec - t1.tv_sec) * 1000.0 +
                (t2.tv_usec - t1.tv_usec) / 1000.0;

    printf("no_cache=%.2f ms, with_cache=%.2f ms\n", d1, d2);

    lab2_close(fd);

    free(buf);
    dlclose(handle);
    return 0;
}

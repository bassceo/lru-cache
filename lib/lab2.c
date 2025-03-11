#include "lab2.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifndef O_DIRECT
#define O_DIRECT 0
#endif

// Small cache configuration
#define BLOCK_SIZE 512
#define CACHE_CAPACITY 16

// Medium cache configuration
// #define BLOCK_SIZE 2048 
// #define CACHE_CAPACITY 64

// Large cache configuration
// #define BLOCK_SIZE 4096
// #define CACHE_CAPACITY 128


typedef struct CacheBlock {
    off_t block_number;
    char *data;
    bool dirty;
    struct CacheBlock *next_hash;
} CacheBlock;

typedef struct Lab2File {
    int fd;
    off_t file_size;
    off_t offset;
    size_t cache_count;
    CacheBlock *hash_table[CACHE_CAPACITY];
} Lab2File;

static Lab2File *files[256];
static int file_index;

static unsigned hash_off(off_t block_number) {
    return (unsigned)(block_number % CACHE_CAPACITY);
}

static void remove_from_hash(Lab2File *f, CacheBlock *b) {
    unsigned i = hash_off(b->block_number);
    CacheBlock *p = f->hash_table[i], *prevp = NULL;
    while (p) {
        if (p == b) {
            if (!prevp) f->hash_table[i] = p->next_hash;
            else prevp->next_hash = p->next_hash;
            return;
        }
        prevp = p;
        p = p->next_hash;
    }
}

static CacheBlock* evict_block(Lab2File *f) {
    if (f->cache_count == 0) return NULL;
    
    unsigned bucket = rand() % CACHE_CAPACITY;
    unsigned original_bucket = bucket;
    CacheBlock *b = NULL;
    CacheBlock *prev = NULL;

    while (!f->hash_table[bucket] && bucket < CACHE_CAPACITY) bucket++;
    if (bucket == CACHE_CAPACITY) {
        bucket = 0;
        while (!f->hash_table[bucket] && bucket < original_bucket) bucket++;
    }

    if (!f->hash_table[bucket]) return NULL;

    b = f->hash_table[bucket];
    if (b->dirty) {
        off_t off = b->block_number * BLOCK_SIZE;
        pwrite(f->fd, b->data, BLOCK_SIZE, off);
    }

    f->hash_table[bucket] = b->next_hash;
    f->cache_count--;
    return b;
}

static CacheBlock* find_block(Lab2File *f, off_t block_num) {
    unsigned i = hash_off(block_num);
    CacheBlock *b = f->hash_table[i];
    while (b) {
        if (b->block_number == block_num) return b;
        b = b->next_hash;
    }
    return NULL;
}

static CacheBlock* load_block(Lab2File *f, off_t block_num) {
    if (f->cache_count >= CACHE_CAPACITY) {
        CacheBlock *victim = evict_block(f);
        if (victim) {
            free(victim->data);
            free(victim);
        }
    }
    CacheBlock *b = malloc(sizeof(CacheBlock));
    posix_memalign((void**)&b->data, BLOCK_SIZE, BLOCK_SIZE);
    b->block_number = block_num;
    b->dirty = false;
    b->next_hash = NULL;
    {
        off_t off = block_num * BLOCK_SIZE;
        ssize_t r = pread(f->fd, b->data, BLOCK_SIZE, off);
        if (r < 0) memset(b->data, 0, BLOCK_SIZE);
        else if (r < BLOCK_SIZE) memset(b->data + r, 0, BLOCK_SIZE - r);
    }
    {
        unsigned i = hash_off(block_num);
        b->next_hash = f->hash_table[i];
        f->hash_table[i] = b;
    }
    f->cache_count++;
    return b;
}

static Lab2File* get_file(int idx) {
    if (idx < 0 || idx >= 256) return NULL;
    return files[idx];
}

int lab2_open(const char *path) {
    static bool seed_initialized = false;
    if (!seed_initialized) {
        srand(time(NULL));
        seed_initialized = true;
    }

    int real_fd = open(path, O_CREAT | O_RDWR | O_DIRECT, 0666);
    if (real_fd < 0) return -1;
    Lab2File *lf = malloc(sizeof(Lab2File));
    memset(lf, 0, sizeof(Lab2File));
    lf->fd = real_fd;
    lf->offset = 0;
    lf->cache_count = 0;
    memset(lf->hash_table, 0, sizeof(lf->hash_table));
    lf->file_size = lseek(real_fd, 0, SEEK_END);
    files[file_index] = lf;
    file_index++;
    return file_index - 1;
}

int lab2_close(int fd) {
    Lab2File *f = get_file(fd);
    if (!f) return -1;
    for (;;) {
        CacheBlock *b = f->hash_table[0];
        if (!b) break;
        if (b->dirty) {
            off_t off = b->block_number * BLOCK_SIZE;
            pwrite(f->fd, b->data, BLOCK_SIZE, off);
        }
        remove_from_hash(f, b);
        free(b->data);
        free(b);
    }
    close(f->fd);
    free(f);
    files[fd] = NULL;
    return 0;
}

ssize_t lab2_read(int fd, void *buf, size_t count) {
    Lab2File *f = get_file(fd);
    if (!f) return -1;

    if (f->offset >= f->file_size) {
        return 0;
    }

    if (f->offset + count > f->file_size) {
        count = f->file_size - f->offset;
    }

    size_t total = 0;
    char *p = buf;
    while (count > 0) {
        off_t bn = f->offset / BLOCK_SIZE;
        size_t off = f->offset % BLOCK_SIZE;
        size_t can_read = BLOCK_SIZE - off;
        if (can_read > count) {
            can_read = count;
        }
        CacheBlock *b = find_block(f, bn);
        if (!b) {
            b = load_block(f, bn);
        }
        memcpy(p, b->data + off, can_read);
        total += can_read;
        p += can_read;
        f->offset += can_read;
        count -= can_read;
    }
    return total;
}

ssize_t lab2_write(int fd, const void *buf, size_t count) {
    Lab2File *f = get_file(fd);
    if (!f) return -1;
    size_t total = 0;
    const char *p = buf;
    while (count > 0) {
        off_t bn = f->offset / BLOCK_SIZE;
        size_t off = f->offset % BLOCK_SIZE;
        size_t can_write = BLOCK_SIZE - off;
        if (can_write > count) can_write = count;
        CacheBlock *b = find_block(f, bn);
        if (!b) {
            if (off != 0 || can_write < BLOCK_SIZE) b = load_block(f, bn);
            else {
                if (f->cache_count >= CACHE_CAPACITY) {
                    CacheBlock *victim = evict_block(f);
                    if (victim) {
                        free(victim->data);
                        free(victim);
                    }
                }
                b = malloc(sizeof(CacheBlock));
                posix_memalign((void**)&b->data, BLOCK_SIZE, BLOCK_SIZE);
                memset(b->data, 0, BLOCK_SIZE);
                b->block_number = bn;
                b->dirty = false;
                b->next_hash = NULL;
                {
                    unsigned i = hash_off(bn);
                    b->next_hash = f->hash_table[i];
                    f->hash_table[i] = b;
                }
                f->cache_count++;
            }
        }
        memcpy(b->data + off, p, can_write);
        b->dirty = true;
        total += can_write;
        p += can_write;
        f->offset += can_write;
        if (f->offset > f->file_size) f->file_size = f->offset;
        count -= can_write;
        if (f->cache_count > CACHE_CAPACITY) {
            CacheBlock *victim = evict_block(f);
            if (victim) {
                free(victim->data);
                free(victim);
            }
        }
    }
    return total;
}

off_t lab2_lseek(int fd, off_t offset, int whence) {
    Lab2File *f = get_file(fd);
    if (!f) return -1;
    off_t new_off;
    if (whence == SEEK_SET) new_off = offset;
    else if (whence == SEEK_CUR) new_off = f->offset + offset;
    else if (whence == SEEK_END) new_off = f->file_size + offset;
    else return -1;
    if (new_off < 0) return -1;
    f->offset = new_off;
    return f->offset;
}

int lab2_fsync(int fd) {
    Lab2File *f = get_file(fd);
    if (!f) return -1;
    CacheBlock *b = f->hash_table[0];
    while (b) {
        if (b->dirty) {
            off_t off = b->block_number * BLOCK_SIZE;
            pwrite(f->fd, b->data, BLOCK_SIZE, off);
            b->dirty = false;
        }
        b = b->next_hash;
    }
    fsync(f->fd);
    return 0;
}

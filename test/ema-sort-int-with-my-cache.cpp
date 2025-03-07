#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <queue>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <sys/stat.h>
#include <errno.h>

using namespace std;

typedef int     (*lab2_open_t)(const char*);
typedef int     (*lab2_close_t)(int);
typedef ssize_t (*lab2_read_t)(int, void*, size_t);
typedef ssize_t (*lab2_write_t)(int, const void*, size_t);
typedef off_t   (*lab2_lseek_t)(int, off_t, int);

static lab2_open_t  lab2_open  = nullptr;
static lab2_close_t lab2_close = nullptr;
static lab2_read_t  lab2_read  = nullptr;
static lab2_write_t lab2_write = nullptr;
static lab2_lseek_t lab2_lseek = nullptr;

bool load_lab2_library(const char* lib_path = "./liblab2.so") {
    void* handle = dlopen(lib_path, RTLD_LAZY);
    if (!handle) return false;
    lab2_open  = (lab2_open_t)  dlsym(handle, "lab2_open");
    lab2_close = (lab2_close_t) dlsym(handle, "lab2_close");
    lab2_read  = (lab2_read_t)  dlsym(handle, "lab2_read");
    lab2_write = (lab2_write_t) dlsym(handle, "lab2_write");
    lab2_lseek = (lab2_lseek_t) dlsym(handle, "lab2_lseek");
    char* error = dlerror();
    if (error) {
        dlclose(handle);
        return false;
    }
    return true;
}

void write_chunk(const vector<int>& chunk, const string& filename) {
    int fd = lab2_open(filename.c_str());
    int size = chunk.size();
    lab2_write(fd, &size, sizeof(size));
    lab2_write(fd, chunk.data(), size * sizeof(int));
    lab2_close(fd);
}

vector<int> read_chunk(const string& filename) {
    int fd = lab2_open(filename.c_str());
    int size;
    lab2_read(fd, &size, sizeof(size));
    vector<int> chunk(size);
    lab2_read(fd, chunk.data(), size * sizeof(int));
    lab2_close(fd);
    return chunk;
}

void merge_files(const vector<string>& chunk_files, const string& output_file) {
    vector<int> fds(chunk_files.size(), -1);
    priority_queue<pair<int,int>, vector<pair<int,int>>, greater<pair<int,int>>> heap;
    for (size_t i = 0; i < chunk_files.size(); i++) {
        fds[i] = lab2_open(chunk_files[i].c_str());
        int value;
        if (lab2_read(fds[i], &value, sizeof(value)) == (ssize_t)sizeof(value)) {
            heap.push({value, (int)i});
        }
    }
    int out_fd = lab2_open(output_file.c_str());
    while (!heap.empty()) {
        auto [value, index] = heap.top();
        heap.pop();
        lab2_write(out_fd, &value, sizeof(value));
        int next_value;
        if (lab2_read(fds[index], &next_value, sizeof(next_value)) == (ssize_t)sizeof(next_value)) {
            heap.push({next_value, index});
        }
    }
    lab2_close(out_fd);
    for (auto& fd : fds) lab2_close(fd);
}

vector<int> ema_sort_int(const vector<int>& arr, const string& temp_dir) {
    load_lab2_library("./liblab2.so");
    const size_t CHUNK_SIZE = 1024 * 1024;
    vector<string> chunk_files;
    filesystem::create_directories(temp_dir);
    for (size_t i = 0; i < arr.size(); i += CHUNK_SIZE) {
        vector<int> chunk(arr.begin() + i, arr.begin() + min(i + CHUNK_SIZE, arr.size()));
        double ema = chunk[0];
        for (size_t j = 1; j < chunk.size(); j++) {
            ema = 0.5 * chunk[j] + 0.5 * ema;
        }
        sort(chunk.begin(), chunk.end());
        string chunk_file = temp_dir + "/chunk_" + to_string(i / CHUNK_SIZE) + ".tmp";
        write_chunk(chunk, chunk_file);
        chunk_files.push_back(chunk_file);
    }
    string output_file = temp_dir + "/sorted.tmp";
    merge_files(chunk_files, output_file);
    vector<int> result = read_chunk(output_file);
    for (const auto& file : chunk_files) filesystem::remove(file);
    filesystem::remove(output_file);
    return result;
}

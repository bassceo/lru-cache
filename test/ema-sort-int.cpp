#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <queue>
#include <algorithm>

using namespace std;

void write_chunk(const vector<int>& chunk, const string& filename) {
    ofstream out(filename, ios::binary);
    int size = chunk.size();
    out.write(reinterpret_cast<const char*>(&size), sizeof(size));
    out.write(reinterpret_cast<const char*>(chunk.data()), size * sizeof(int));
}

vector<int> read_chunk(const string& filename) {
    ifstream in(filename, ios::binary);
    int size;
    in.read(reinterpret_cast<char*>(&size), sizeof(size));
    vector<int> chunk(size);
    in.read(reinterpret_cast<char*>(chunk.data()), size * sizeof(int));
    return chunk;
}

void merge_files(const vector<string>& chunk_files, const string& output_file) {
    vector<ifstream> inputs(chunk_files.size());
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<>> heap;

    for (size_t i = 0; i < chunk_files.size(); i++) {
        inputs[i].open(chunk_files[i], ios::binary);
        int value;
        if (inputs[i].read(reinterpret_cast<char*>(&value), sizeof(int))) {
            heap.push({value, i});
        }
    }

    ofstream out(output_file, ios::binary);
    
    while (!heap.empty()) {
        auto [value, index] = heap.top();
        heap.pop();
        
        out.write(reinterpret_cast<const char*>(&value), sizeof(int));
        
        int next_value;
        if (inputs[index].read(reinterpret_cast<char*>(&next_value), sizeof(int))) {
            heap.push({next_value, index});
        }
    }
}

vector<int> ema_sort_int(const vector<int>& arr, const string& temp_dir) {
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

    for (const auto& file : chunk_files) {
        filesystem::remove(file);
    }
    filesystem::remove(output_file);

    return result;
}
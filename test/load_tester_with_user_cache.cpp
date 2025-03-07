#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <random>
#include "ema-sort-int-with-my-cache.h"

using namespace std;
namespace fs = std::filesystem;

void worker_thread(int iterations) {
    vector<int> data(1000);

    stringstream ss;
    ss << "temp_" << this_thread::get_id();
    string temp_dir = ss.str();
    fs::create_directory(temp_dir);

    random_device rd;
    mt19937 generator(rd());
    uniform_int_distribution<int> distribution(0, 9999);

    for (int i = 0; i < iterations; i++) {
        for (int j = 0; j < 1000; j++) {
            data[j] = distribution(generator);
        }
        ema_sort_int(data, temp_dir);
    }

    fs::remove_all(temp_dir);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <num_threads> <iterations>" << endl;
        cerr << "  <num_threads>: number of threads" << endl;
        cerr << "  <iterations>: number of iterations per thread" << endl;
        return 1;
    }

    int num_threads = stoi(argv[1]);
    int iterations = stoi(argv[2]);

    vector<thread> threads;
    auto start = chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker_thread, iterations);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::seconds>(end - start);
    auto duration_precise = chrono::duration_cast<chrono::duration<double>>(end - start);

    cout << "CPU load completed with " << num_threads << " threads and "
         << iterations << " iterations per thread. Execution time: "
         << duration_precise.count() << " seconds." << endl;

    return 0;
}
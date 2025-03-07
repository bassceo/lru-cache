#include <iostream>
#include <vector>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <random>
#include "ema-sort-int.h"

using namespace std;
namespace fs = std::filesystem;

void run_test(int iterations) {
    vector<int> data(1000);

    string temp_dir = "temp_dir";
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

int main() {
    int iterations = 1000; // Set a default number of iterations

    auto start = chrono::high_resolution_clock::now();

    run_test(iterations);

    auto end = chrono::high_resolution_clock::now();
    auto duration_precise = chrono::duration_cast<chrono::duration<double>>(end - start);

    cout << "CPU load completed with " << iterations << " iterations. Execution time: "
         << duration_precise.count() << " seconds." << endl;

    return 0;
}

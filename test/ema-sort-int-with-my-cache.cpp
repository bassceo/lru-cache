#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <queue>
#include <algorithm>
#include <functional>
#include <sys/stat.h>
#include <errno.h>
#include <dlfcn.h>

// Определяем типы указателей на функции, которые хотим вытащить из liblab2.so
typedef int     (*lab2_open_t)(const char *);
typedef int     (*lab2_close_t)(int);
typedef ssize_t (*lab2_read_t)(int, void *, size_t);
typedef ssize_t (*lab2_write_t)(int, const void *, size_t);
typedef off_t   (*lab2_lseek_t)(int, off_t, int);

// Глобальные (или статические) переменные для хранения указателей на функции
static lab2_open_t  lab2_open  = nullptr;
static lab2_close_t lab2_close = nullptr;
static lab2_read_t  lab2_read  = nullptr;
static lab2_write_t lab2_write = nullptr;
static lab2_lseek_t lab2_lseek = nullptr;

// Функция, загружающая библиотеку и привязывающая наши указатели
bool load_lab2_library(const char* lib_path = "./liblab2.so") {
    void* handle = dlopen(lib_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "Cannot open library %s: %s\n", lib_path, dlerror());
        return false;
    }

    // Вытаскиваем символы
    lab2_open  = (lab2_open_t) dlsym(handle, "lab2_open");
    lab2_close = (lab2_close_t)dlsym(handle, "lab2_close");
    lab2_read  = (lab2_read_t) dlsym(handle, "lab2_read");
    lab2_write = (lab2_write_t)dlsym(handle, "lab2_write");
    lab2_lseek = (lab2_lseek_t)dlsym(handle, "lab2_lseek");

    char* error;
    if ((error = dlerror()) != NULL) {
        fprintf(stderr, "Error dlsym: %s\n", error);
        dlclose(handle);
        return false;
    }

    return true;
}

// Пример функции записи куска (chunk) целых чисел
// Вместо ofstream используем lab2_open/lab2_write/lab2_close
bool write_chunk(const std::vector<int>& chunk, const std::string& filename) {
    // Предположим, что lab2_open открывает файл в режиме чтения/записи, 
    // но на практике может потребоваться отдельная функция для создания (O_CREAT).
    // Ниже — условная логика. В реальности нужен lab2_open с флагами.
    int fd = lab2_open(filename.c_str());
    if (fd < 0) {
        fprintf(stderr, "write_chunk: cannot open file %s\n", filename.c_str());
        return false;
    }

    // Сначала пишем кол-во элементов
    int size = (int)chunk.size();
    ssize_t written = lab2_write(fd, &size, sizeof(size));
    if (written < 0) {
        fprintf(stderr, "write_chunk: error writing size\n");
        lab2_close(fd);
        return false;
    }

    // Потом пишем сами числа
    ssize_t total_bytes = size * sizeof(int);
    written = lab2_write(fd, chunk.data(), total_bytes);
    if (written < 0) {
        fprintf(stderr, "write_chunk: error writing chunk data\n");
        lab2_close(fd);
        return false;
    }

    lab2_close(fd);
    return true;
}

// Читаем кусок (chunk) целых чисел
// Вместо ifstream — lab2_open/lab2_read/lab2_close
std::vector<int> read_chunk(const std::string& filename) {
    std::vector<int> result;

    int fd = lab2_open(filename.c_str());
    if (fd < 0) {
        fprintf(stderr, "read_chunk: cannot open file %s\n", filename.c_str());
        return result; // вернём пустой
    }

    int size = 0;
    ssize_t rd = lab2_read(fd, &size, sizeof(size));
    if (rd < 0 || rd < (ssize_t)sizeof(size)) {
        fprintf(stderr, "read_chunk: error reading size from %s\n", filename.c_str());
        lab2_close(fd);
        return result;
    }

    result.resize(size);
    ssize_t to_read = size * sizeof(int);
    rd = lab2_read(fd, result.data(), to_read);
    if (rd < 0 || rd < to_read) {
        fprintf(stderr, "read_chunk: error reading chunk data from %s\n", filename.c_str());
        lab2_close(fd);
        // можно вернуть то, что уже успели прочитать (или пустой)
        result.clear(); 
        return result;
    }

    lab2_close(fd);
    return result;
}

// Сливаем (merge) несколько отсортированных файлов в один общий
// Вместо ifstream/ofstream — снова функции из динамической библиотеки
bool merge_files(const std::vector<std::string>& chunk_files, const std::string& output_file) {
    // Открываем все файлы на чтение
    std::vector<int> fds(chunk_files.size(), -1);
    for (size_t i = 0; i < chunk_files.size(); i++) {
        fds[i] = lab2_open(chunk_files[i].c_str());
        if (fds[i] < 0) {
            fprintf(stderr, "merge_files: can't open chunk file %s\n", chunk_files[i].c_str());
            // Нужно аккуратно позакрывать уже открытые
            for (size_t j = 0; j < i; j++) {
                if (fds[j] >= 0) {
                    lab2_close(fds[j]);
                }
            }
            return false;
        }
        // Пропускаем 4 байта размера (в начале каждого чанка),
        // так как мы уже его знаем, и нам нужна только "полоска" чисел для слияния.
        // Но вообще можно и вычитать, если логика другая — здесь зависит от задумки.
        // Для демонстрации пропустим заголовок (sizeof(int)).
        off_t off = lab2_lseek(fds[i], sizeof(int), SEEK_SET);
        if (off < 0) {
            fprintf(stderr, "merge_files: lseek error\n");
            for (auto& f : fds) {
                if (f >= 0) lab2_close(f);
            }
            return false;
        }
    }

    // Открываем выходной файл для записи
    int out_fd = lab2_open(output_file.c_str());
    if (out_fd < 0) {
        fprintf(stderr, "merge_files: cannot open output file %s\n", output_file.c_str());
        for (auto& f : fds) {
            if (f >= 0) lab2_close(f);
        }
        return false;
    }

    // Здесь используем min-кучу для слияния "k-полос" (k-way merge)
    // Будем читать по одному int из каждого файла и закидывать в приоритетную очередь
    // Пара (value, index), где value — прочитанное число, index — откуда прочли
    std::priority_queue<
        std::pair<int, int>,
        std::vector<std::pair<int,int>>,
        std::greater<std::pair<int,int>>
    > heap;

    // Считаем по одному int из каждого файла
    for (size_t i = 0; i < chunk_files.size(); i++) {
        int val;
        ssize_t rd = lab2_read(fds[i], &val, sizeof(val));
        if (rd == (ssize_t)sizeof(val)) {
            heap.push({val, (int)i});
        }
    }

    // Теперь, пока куча не опустеет, выбираем наименьший элемент, пишем его в out_fd
    // и подгружаем следующий int из соответствующего файла
    while (!heap.empty()) {
        auto [value, index] = heap.top();
        heap.pop();
        
        // Пишем во входной файл (без заголовка)
        ssize_t wr = lab2_write(out_fd, &value, sizeof(value));
        if (wr < 0) {
            fprintf(stderr, "merge_files: error writing to output\n");
            break; // завершаем принудительно
        }

        int next_val;
        ssize_t rd = lab2_read(fds[index], &next_val, sizeof(next_val));
        if (rd == (ssize_t)sizeof(next_val)) {
            // положили в кучу
            heap.push({next_val, index});
        }
        // если не удалось прочитать, значит этот файл закончился
    }

    // Закрываем все файлы
    lab2_close(out_fd);
    for (auto& f : fds) {
        if (f >= 0) lab2_close(f);
    }

    return true;
}

// Основная функция, аналогичная ema_sort_int:
// разбивает массив на чанки, считает (для примера) EMA, сортирует каждый чанк и пишет во временные файлы,
// затем сливает всё в один отсортированный файл, считывает результат, 
// и удаляет временные.
std::vector<int> ema_sort_int(const std::vector<int>& arr, const std::string& temp_dir) {
    const size_t CHUNK_SIZE = 1024 * 1024; // к примеру
    std::vector<std::string> chunk_files;
    
    // Создадим директорию temp_dir (если надо)
    // В реальном коде желательно проверять существование, права, etc.
    if (mkdir(temp_dir.c_str(), 0777) < 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create directory %s\n", temp_dir.c_str());
        return {};
    }

    // Разбиваем на чанки
    for (size_t i = 0; i < arr.size(); i += CHUNK_SIZE) {
        std::vector<int> chunk(
            arr.begin() + i, 
            arr.begin() + std::min(i + CHUNK_SIZE, arr.size())
        );
        
        // Примерно считаем EMA (экспоненциальное среднее), 
        // хотя реальный результат никуда не используется, кроме демонстрации логики
        double ema = chunk[0];
        for (size_t j = 1; j < chunk.size(); j++) {
            ema = 0.5 * chunk[j] + 0.5 * ema;
        }

        // Сортируем чанк
        std::sort(chunk.begin(), chunk.end());
        
        // Пишем во временный файл
        std::string chunk_file = temp_dir + "/chunk_" + std::to_string(i / CHUNK_SIZE) + ".tmp";
        if (!write_chunk(chunk, chunk_file)) {
            fprintf(stderr, "ema_sort_int: write_chunk failed for %s\n", chunk_file.c_str());
            // Можно обработать ошибку, почистить файлы и пр.
        }
        chunk_files.push_back(chunk_file);
    }

    // Сливаем все чанки в один файл
    std::string output_file = temp_dir + "/sorted.tmp";
    if (!merge_files(chunk_files, output_file)) {
        fprintf(stderr, "ema_sort_int: merge_files failed\n");
        // аналогично — обработка
        return {};
    }

    // Читаем результат обратно в вектор
    std::vector<int> result = read_chunk(output_file);

    // Удаляем временные файлы
    // В реальном коде: лучше проверять ошибки, использовать unlink() и т.п.
    for (const auto& file : chunk_files) {
        remove(file.c_str());
    }
    remove(output_file.c_str());

    return result;
}

// Пример main, где подгружаем библиотеку и вызываем функцию сортировки
int main() {
    // Загружаем динамическую библиотеку с нужными функциями
    if (!load_lab2_library("./liblab2.so")) {
        return 1; 
    }

    // Пример данных
    std::vector<int> data = {10, 2, 7, 9, 3, 5, 1, 8, 4, 6};

    // Путь к папке для временных файлов
    std::string temp_dir = "./temp_sort_dir";

    // Вызываем нашу функцию сортировки
    std::vector<int> sorted = ema_sort_int(data, temp_dir);

    // Выводим результат
    printf("Sorted result:\n");
    for (int x : sorted) {
        printf("%d ", x);
    }
    printf("\n");

    return 0;
}

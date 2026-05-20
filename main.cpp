#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iomanip>
#include <sys/stat.h>
#include <dirent.h>
#include <iostream>
#include <cstdlib>
#include <string>
#include <ctime>

extern "C" {
    void set_key(char key);
    void caesar(void* src, void* dst, int len);
}

#ifndef WORKERS_COUNT
#define WORKERS_COUNT 4
#endif

// Глобальные структуры для очереди задач
struct FileTask {
    std::string in_path;
    std::string out_dir;
};

std::vector<FileTask> task_queue;
size_t queue_head = 0;
bool finished_adding = false;

std::mutex queue_mutex;
std::condition_variable cv;
std::mutex log_mutex;

// 1. Сбор файлов
void collect_files(const std::string& path, std::vector<std::string>& files) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return;

    if (S_ISREG(st.st_mode)) {
        files.push_back(path);
    } else if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path.c_str());
        if (!dir) return;
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            collect_files(path + "/" + name, files);
        }
        closedir(dir);
    }
}

// 2. Создание папок
void mkdirs(const std::string& path) {
    size_t pos = 0;
    if (path[0] == '/') pos = 1;
    else if (path.substr(0, 2) == "./") pos = 1;
    
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        mkdir(path.substr(0, pos).c_str(), 0755);
        pos++; 
    }
}

// 3. Обработка файла (возвращает время выполнения)
double process_file_task(const std::string& in_path, const std::string& out_dir) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    std::ifstream in(in_path, std::ios::binary | std::ios::ate);
    if (!in) return 0.0;
    
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!in.read(buffer.data(), size)) return 0.0;

    // XOR шифрование
    caesar(buffer.data(), buffer.data(), static_cast<int>(size));

    std::string filename = in_path.substr(in_path.find_last_of("/\\") + 1);
    std::string out_path = out_dir + (out_dir.back() == '/' ? "" : "/") + filename;
    
    mkdirs(out_path);

    std::ofstream out(out_path, std::ios::binary);
    if (out) out.write(buffer.data(), size);

    // Лог
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::ofstream log("log.txt", std::ios::app);
        if (log) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            log << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") 
                << " " << in_path << "\n";
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

// Функция потока из пула
void worker_func(std::vector<double>& times, std::mutex& times_mutex) {
    while (true) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        // Ждем, пока есть задачи или работа завершена
        cv.wait(lock, [] { return queue_head < task_queue.size() || finished_adding; });

        if (queue_head >= task_queue.size()) break;

        // Забираем задачу
        FileTask task = task_queue[queue_head++];
        lock.unlock();

        double t = process_file_task(task.in_path, task.out_dir);
        
        {
            std::lock_guard<std::mutex> t_lock(times_mutex);
            times.push_back(t);
        }
    }
}

enum Mode { SEQUENTIAL, PARALLEL, AUTO };

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " [--mode=seq|par|auto] <files...> <key> <out_dir>\n";
        return 1;
    }

    Mode mode = AUTO;
    int start_idx = 1;

    // Парсинг режима
    if (std::string(argv[1]).find("--mode=") == 0) {
        std::string m = argv[1];
        if (m == "--mode=seq") mode = SEQUENTIAL;
        else if (m == "--mode=par") mode = PARALLEL;
        else mode = AUTO;
        start_idx = 2;
    }

    const char* out_dir = argv[argc - 1];
    int key = std::atoi(argv[argc - 2]) & 0xFF;
    
    // Сбор файлов
    std::vector<std::string> files;
    for (int i = start_idx; i < argc - 2; ++i) {
        collect_files(argv[i], files);
    }

    if (files.empty()) {
        std::cerr << "No files found.\n";
        return 1;
    }

    set_key(static_cast<char>(key));
    std::ofstream("log.txt", std::ios::trunc); // Очистка лога

    // Авто-выбор режима
    if (mode == AUTO) {
        mode = (files.size() < 5) ? SEQUENTIAL : PARALLEL;
    }

    std::cout << "Mode: " << (mode == SEQUENTIAL ? "SEQUENTIAL" : "PARALLEL") << "\n";

    struct timespec global_start, global_end;
    clock_gettime(CLOCK_MONOTONIC, &global_start);

    std::vector<double> file_times;
    file_times.reserve(files.size());

    if (mode == SEQUENTIAL) {
        // Последовательная обработка
        for (const auto& f : files) {
            double t = process_file_task(f, out_dir);
            file_times.push_back(t);
        }
    } else {
        // Параллельная обработка (Пул потоков)
        std::mutex times_mutex;
        
        // Заполняем очередь
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            for (const auto& f : files) {
                task_queue.push_back({f, out_dir});
            }
            finished_adding = false;
        }

        std::vector<std::thread> workers;
        for (int i = 0; i < WORKERS_COUNT; ++i) {
            workers.emplace_back(worker_func, std::ref(file_times), std::ref(times_mutex));
        }

        // Сигнал старта
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            finished_adding = true;
        }
        cv.notify_all();

        for (auto& w : workers) w.join();
    }

    clock_gettime(CLOCK_MONOTONIC, &global_end);
    double total_time = (global_end.tv_sec - global_start.tv_sec) + 
                        (global_end.tv_nsec - global_start.tv_nsec) / 1e9;
    
    double avg_time = 0;
    if (!file_times.empty()) {
        double sum = 0;
        for (double t : file_times) sum += t;
        avg_time = sum / file_times.size();
    }

    // Вывод статистики
    std::cout << "\n--- Statistics ---\n";
    std::cout << "Total time:   " << std::fixed << std::setprecision(4) << total_time << " s\n";
    std::cout << "Avg per file: " << std::fixed << std::setprecision(4) << avg_time << " s\n";
    std::cout << "Files:        " << file_times.size() << "\n";

    // Сравнение для авто-режима
    if (mode == AUTO) {
        std::cout << "\n--- Comparison ---\n";
        if (files.size() >= 5) {
            // Parallel. Seq = сумма всех времен.
            double seq_est = 0;
            for(double t : file_times) seq_est += t;
            std::cout << "Used (Parallel):  " << total_time << " s\n";
            std::cout << "Alt (Sequential): " << seq_est << " s\n";
        } else {
            // Sequential. Par = примерно общее / кол-во потоков.
            std::cout << "Used (Sequential): " << total_time << " s\n";
            std::cout << "Alt (Parallel):    ~" << total_time / WORKERS_COUNT << " s\n";
        }
    }

    return 0;
}
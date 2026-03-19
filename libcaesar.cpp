#include <cstdint>
#include <fstream>
#include <vector>
#include <cstring>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>

static char g_key = 0;
static std::mutex log_mutex;

extern "C" {

    void set_key(char key) {
        g_key = key;
    }

    void caesar(void* src, void* dst, int len) {
        if (len <= 0 || !src || !dst) return;
        unsigned char* s = static_cast<unsigned char*>(src);
        unsigned char* d = static_cast<unsigned char*>(dst);
        for (int i = 0; i < len; ++i) {
            d[i] = s[i] ^ static_cast<unsigned char>(g_key);
        }
    }

    // Структура для передачи данных в поток
    struct FileTask {
        const char* input_path;
        const char* out_dir;
        int thread_id;
    };

    // Функция-воркер для потока
    void process_file_task(FileTask* task) {
        struct stat path_stat;
        if (stat(task->input_path, &path_stat) != 0 || !S_ISREG(path_stat.st_mode)) {
            return;
        }

        // Таймлок 5 секунд
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        auto start = std::chrono::high_resolution_clock::now();

        std::ifstream in(task->input_path, std::ios::binary);
        if (!in) {
            return;
        }

        std::vector<char> data((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
        in.close();

        if (!data.empty()) {
            caesar(data.data(), data.data(), data.size());
        }

        std::string out_path = task->out_dir;
        if (out_path.back() != '/') out_path += '/';
        
        std::string filepath = task->input_path;
        size_t pos = filepath.find_last_of("/\\");
        out_path += (pos != std::string::npos) ? filepath.substr(pos + 1) : filepath;

        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            return;
        }
        out.write(data.data(), data.size());
        out.close();

        // Запись в лог с синхронизацией
        std::lock_guard<std::mutex> lock(log_mutex);
        std::ofstream log("log.txt", std::ios::app);
        if (log) {
            auto end_time = std::chrono::system_clock::now();
            auto time_t_end = std::chrono::system_clock::to_time_t(end_time);
            log << std::put_time(std::localtime(&time_t_end), "%Y-%m-%d %H:%M:%S")
                << " thread-" << task->thread_id
                << " " << filepath << "\n";
        }
        log.close();
    }

    int process_files(const char** file_paths, int count, const char* out_dir) {
        const int NUM_THREADS = 3;
        std::vector<std::thread> threads;
        std::vector<FileTask> tasks;

        // Очищаем лог перед началом
        std::ofstream log_clear("log.txt", std::ios::trunc);
        log_clear.close();

        for (int i = 0; i < count; ++i) {
            int thread_id = i % NUM_THREADS;
            tasks.push_back({file_paths[i], out_dir, thread_id});
        }

        // Запускаем потоки
        for (int i = 0; i < count; ++i) {
            threads.emplace_back(process_file_task, &tasks[i]);
        }

        // Ждём завершения всех потоков
        for (auto& t : threads) {
            t.join();
        }

        return 0;
    }

}
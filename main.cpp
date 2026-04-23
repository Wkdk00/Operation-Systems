#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sys/stat.h>
#include <dirent.h>
#include <iostream>
#include <cstdlib>
#include <string>

extern "C" {
    void set_key(char key);
    void caesar(void* src, void* dst, int len);
}

static std::mutex log_mutex;

// 1. Рекурсивный сбор файлов
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

// 2. Создание директорий
void mkdirs(const std::string& path) {
    size_t pos = 0;
    if (path.substr(0, 2) == "./") pos = 1;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        mkdir(path.substr(0, pos).c_str(), 0755);
    }
}

// 3. Обработка одного файла (задача для потока)
void process_file(const std::string& in_path, const std::string& out_dir, int tid) {
    std::ifstream in(in_path, std::ios::binary | std::ios::ate);
    if (!in) return;
    
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!in.read(buffer.data(), size)) return;

    // Шифрование на месте
    caesar(buffer.data(), buffer.data(), static_cast<int>(size));

    // Формируем выходной путь
    std::string filename = in_path.substr(in_path.find_last_of("/\\") + 1);
    std::string out_path = out_dir + (out_dir.back() == '/' ? "" : "/") + filename;
    
    mkdirs(out_path);

    std::ofstream out(out_path, std::ios::binary);
    if (out) out.write(buffer.data(), size);

    // Лог
    std::lock_guard<std::mutex> lock(log_mutex);
    std::ofstream log("log.txt", std::ios::app);
    if (log) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        log << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") 
            << " thread-" << tid << " " << in_path << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <file/dir...> <key> <out_dir>\n";
        return 1;
    }

    const char* out_dir = argv[argc - 1];
    int key = std::atoi(argv[argc - 2]) & 0xFF;
    
    // Сбор всех файлов
    std::vector<std::string> files;
    for (int i = 1; i < argc - 2; ++i) {
        collect_files(argv[i], files);
    }

    if (files.empty()) {
        std::cerr << "No files found.\n";
        return 1;
    }

    set_key(static_cast<char>(key));
    
    // Очистка лога
    std::ofstream("log.txt", std::ios::trunc);

    // Запуск потоков (максимум 3, как было ранее)
    const int N_THREADS = 3;
    std::vector<std::thread> threads;
    
    for (size_t i = 0; i < files.size(); ++i) {
        // Если достигли лимита потоков, ждём завершения старых
        if (threads.size() >= N_THREADS) {
            threads.front().join();
            threads.erase(threads.begin());
        }
        // Запускаем новый поток
        threads.emplace_back(process_file, files[i], out_dir, static_cast<int>(i % N_THREADS));
    }

    // Ждём остальные
    for (auto& t : threads) t.join();

    std::cout << "Done. Processed " << files.size() << " files.\n";
    return 0;
}
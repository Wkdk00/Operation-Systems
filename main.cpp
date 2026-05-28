#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sys/stat.h>
#include <dirent.h>
#include <iostream>
#include <cstdlib>
#include <string>
#include <ctime>
#include <algorithm>

extern "C" {
    void rc4_crypt(const uint8_t* key, int key_len, const uint8_t* in, uint8_t* out, int data_len);
}

#ifndef WORKERS_COUNT
#define WORKERS_COUNT 5
#endif

struct FileTask {
    std::string disk_path;
    std::string arch_name;
};

std::vector<FileTask> task_queue;
size_t queue_head = 0;
bool finished_adding = false;

std::mutex queue_mutex;
std::condition_variable cv;
std::mutex image_mutex;

std::string global_image_path;
std::string global_key;

// 1. Сбор файлов (с учетом вложенности директорий)
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
            collect_files(path + (path.back() == '/' ? "" : "/") + name, files);
        }
        closedir(dir);
    }
}

// 2. Обработка файла (Шифрование RC4 и добавление в образ)
void process_add_task(const FileTask& task) {
    std::ifstream in(task.disk_path, std::ios::binary | std::ios::ate);
    if (!in) {
        std::cerr << "Error reading: " << task.disk_path << "\n";
        return;
    }
    
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (size > 0 && !in.read(reinterpret_cast<char*>(buffer.data()), size)) return;

    // Генерация 16-байтовой соли
    uint8_t salt[16];
    for (int i = 0; i < 16; ++i) salt[i] = rand() % 256;

    // Инициализационная последовательность (Ключ + Соль)
    std::vector<uint8_t> rc4_key(global_key.begin(), global_key.end());
    rc4_key.insert(rc4_key.end(), salt, salt + 16);

    // Шифрование
    rc4_crypt(rc4_key.data(), rc4_key.size(), buffer.data(), buffer.data(), size);

    // Потокобезопасная запись в образ
    std::lock_guard<std::mutex> lock(image_mutex);
    std::ofstream out(global_image_path, std::ios::binary | std::ios::app);
    if (!out) {
        std::cerr << "Error writing to image\n";
        return;
    }

    uint32_t file_len = size;
    uint32_t name_len = task.arch_name.length();

    out.write(reinterpret_cast<char*>(&file_len), 4);
    out.write(reinterpret_cast<char*>(&name_len), 4);
    out.write(reinterpret_cast<char*>(salt), 16);
    out.write(task.arch_name.data(), name_len);
    if (size > 0) out.write(reinterpret_cast<char*>(buffer.data()), file_len);
}

// 3. Функция потока из пула
void worker_func() {
    while (true) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        cv.wait(lock, [] { return queue_head < task_queue.size() || finished_adding; });

        if (queue_head >= task_queue.size()) break;
        FileTask task = task_queue[queue_head++];
        lock.unlock();

        process_add_task(task);
    }
}

// 4. Просмотр перечня файлов
void list_files() {
    std::ifstream in(global_image_path, std::ios::binary);
    if (!in) {
        std::cerr << "Image not found or error opening.\n";
        return;
    }
    std::vector<std::pair<std::string, uint32_t>> files;

    while (in.peek() != EOF) {
        uint32_t file_len, name_len;
        uint8_t salt[16];
        if (!in.read(reinterpret_cast<char*>(&file_len), 4)) break;
        if (!in.read(reinterpret_cast<char*>(&name_len), 4)) break;
        if (!in.read(reinterpret_cast<char*>(salt), 16)) break;

        std::string name(name_len, '\0');
        if (!in.read(&name[0], name_len)) break;

        files.push_back({name, file_len});
        in.seekg(file_len, std::ios::cur);
    }

    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::cout << f.first << " - " << f.second << " bytes\n";
    }
}

// 5. Просмотр (извлечение) содержимого файла
void get_file(const std::string& target, const std::string& out_path) {
    std::ifstream in(global_image_path, std::ios::binary);
    if (!in) {
        std::cerr << "Image not found.\n";
        return;
    }

    while (in.peek() != EOF) {
        uint32_t file_len, name_len;
        uint8_t salt[16];
        if (!in.read(reinterpret_cast<char*>(&file_len), 4)) break;
        if (!in.read(reinterpret_cast<char*>(&name_len), 4)) break;
        if (!in.read(reinterpret_cast<char*>(salt), 16)) break;

        std::string name(name_len, '\0');
        if (!in.read(&name[0], name_len)) break;

        if (name == target) {
            std::vector<uint8_t> buffer(file_len);
            if (file_len > 0 && !in.read(reinterpret_cast<char*>(buffer.data()), file_len)) return;

            std::vector<uint8_t> rc4_key(global_key.begin(), global_key.end());
            rc4_key.insert(rc4_key.end(), salt, salt + 16);

            rc4_crypt(rc4_key.data(), rc4_key.size(), buffer.data(), buffer.data(), file_len);

            std::ofstream out(out_path, std::ios::binary);
            if (file_len > 0) out.write(reinterpret_cast<char*>(buffer.data()), file_len);
            std::cout << "Extracted " << target << " to " << out_path << "\n";
            return;
        } else {
            in.seekg(file_len, std::ios::cur); // Пропуск содержимого других файлов
        }
    }
    std::cerr << "File not found in image.\n";
}

enum Action { ADD, LIST, GET, NONE };

int main(int argc, char* argv[]) {
    Action action = NONE;
    std::string out_file, target_file;
    std::vector<std::string> inputs;

    // Парсинг аргументов
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-add") action = ADD;
        else if (arg == "-list") action = LIST;
        else if (arg == "-get") action = GET;
        else if (arg == "-key" && i + 1 < argc) global_key = argv[++i];
        else if (arg == "-image" && i + 1 < argc) global_image_path = argv[++i];
        else if (arg == "-out" && i + 1 < argc) out_file = argv[++i];
        else {
            if (action == ADD) inputs.push_back(arg);
            else if (action == GET && target_file.empty()) target_file = arg;
        }
    }

    if (global_image_path.empty() || action == NONE) {
        std::cerr << "Invalid arguments. Usage:\n"
                  << "  ./secure_copy -add -key <key> -image <image> <files/dirs...>\n"
                  << "  ./secure_copy -list -image <image>\n"
                  << "  ./secure_copy -get -image <image> -key <key> -out <result> <file_name>\n";
        return 1;
    }

    srand(time(nullptr));

    if (action == ADD) {
        if (global_key.empty() || inputs.empty()) {
            std::cerr << "Key or input files missing for add operation.\n";
            return 1;
        }
        std::vector<std::string> all_files;
        for (const auto& in_p : inputs) collect_files(in_p, all_files);

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            for (const auto& f : all_files) {
                task_queue.push_back({f, f}); // Имя файла сохраняется как путь
            }
            finished_adding = true;
        }
        
        std::vector<std::thread> workers;
        for (int i = 0; i < WORKERS_COUNT; ++i) {
            workers.emplace_back(worker_func);
        }
        cv.notify_all();
        for (auto& w : workers) w.join();

    } else if (action == LIST) {
        list_files();
    } else if (action == GET) {
        if (global_key.empty() || out_file.empty() || target_file.empty()) {
            std::cerr << "Missing arguments for get operation.\n";
            return 1;
        }
        get_file(target_file, out_file);
    }

    return 0;
}
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
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>

extern "C" {
    typedef struct {
        uint8_t S[256];
        int i;
        int j;
    } RC4_CTX;

    void rc4_init(RC4_CTX* ctx, const uint8_t* key, int key_len);
    void rc4_update(RC4_CTX* ctx, const uint8_t* in, uint8_t* out, int data_len);
}

#ifndef WORKERS_COUNT
#define WORKERS_COUNT 5
#endif

const size_t CHUNK_SIZE = 4 * 1024 * 1024; // 4 MB

struct FileTask {
    std::string disk_path;
    std::string arch_name;
    std::size_t image_offset;
};

std::vector<FileTask> task_queue;
size_t queue_head = 0;
bool finished_adding = false;

std::mutex queue_mutex;
std::condition_variable cv;

std::string global_image_path;
uint8_t* global_image_ptr = nullptr; 
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

    // Изолированный потокобезопасный генератор соли для каждого потока
    uint8_t salt[16];
    srand(static_cast<unsigned int>(time(nullptr)));
    for (int i = 0; i < 16; ++i) {
        salt[i] = static_cast<uint8_t>(rand() % 256);
    }

    // Инициализационная последовательность (Ключ + Соль)
    std::vector<uint8_t> rc4_key(global_key.begin(), global_key.end());
    rc4_key.insert(rc4_key.end(), salt, salt + 16);

    size_t page_size = sysconf(_SC_PAGESIZE);
    
    // Выделяем изолированную страницу напрямую у ядра через mmap
    RC4_CTX* ctx = static_cast<RC4_CTX*>(mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (ctx == MAP_FAILED) return;
    
    mlock(ctx, page_size);
    rc4_init(ctx, rc4_key.data(), rc4_key.size());
    mprotect(ctx, page_size, PROT_NONE);

    uint8_t* dst = global_image_ptr + task.image_offset;
    uint32_t file_len = static_cast<uint32_t>(size), name_len = static_cast<uint32_t>(task.arch_name.length());

    memcpy(dst, &file_len, 4);          dst += 4;
    memcpy(dst, &name_len, 4);          dst += 4;
    memcpy(dst, salt, 16);              dst += 16;
    memcpy(dst, task.arch_name.data(), name_len); dst += name_len;

    if (size > 0) {
        std::vector<uint8_t> chunk_buffer(CHUNK_SIZE);
        while (in) {
            in.read(reinterpret_cast<char*>(chunk_buffer.data()), CHUNK_SIZE);
            std::streamsize bytes_read = in.gcount();
            
            if (bytes_read > 0) {
                mprotect(ctx, page_size, PROT_READ | PROT_WRITE);
                rc4_update(ctx, chunk_buffer.data(), chunk_buffer.data(), bytes_read);
                mprotect(ctx, page_size, PROT_NONE);

                memcpy(dst, chunk_buffer.data(), bytes_read);
                dst += bytes_read;
            }
        }
    }

    mprotect(ctx, page_size, PROT_READ | PROT_WRITE);
    memset(ctx, 0, page_size);
    munlock(ctx, page_size);
    munmap(ctx, page_size);
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

    while (true) {
        uint32_t file_len = 0, name_len = 0;
        uint8_t salt[16];

        if (!in.read(reinterpret_cast<char*>(&file_len), 4)) break;
        if (!in.read(reinterpret_cast<char*>(&name_len), 4)) break;
        if (!in.read(reinterpret_cast<char*>(salt), 16)) break;

        std::string name(name_len, '\0');
        if (!in.read(&name[0], name_len)) break;

        if (name == target) {
            std::ofstream out(out_path, std::ios::binary);
            if (!out) { std::cerr << "Failed to open output file: " << out_path << "\n"; return; }

            std::vector<uint8_t> rc4_key(global_key.begin(), global_key.end());
            rc4_key.insert(rc4_key.end(), salt, salt + 16);

            size_t page_size = sysconf(_SC_PAGESIZE);
            RC4_CTX* ctx = static_cast<RC4_CTX*>(mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
            if (ctx == MAP_FAILED) return;

            mlock(ctx, page_size);
            rc4_init(ctx, rc4_key.data(), rc4_key.size());
            mprotect(ctx, page_size, PROT_NONE);

            uint32_t bytes_remaining = file_len;
            std::vector<uint8_t> chunk_buffer(CHUNK_SIZE);

            while (bytes_remaining > 0) {
                uint32_t to_read = std::min(static_cast<uint32_t>(CHUNK_SIZE), bytes_remaining);
                if (!in.read(reinterpret_cast<char*>(chunk_buffer.data()), to_read)) break;

                mprotect(ctx, page_size, PROT_READ | PROT_WRITE);
                rc4_update(ctx, chunk_buffer.data(), chunk_buffer.data(), to_read);
                mprotect(ctx, page_size, PROT_NONE);

                out.write(reinterpret_cast<char*>(chunk_buffer.data()), to_read);
                bytes_remaining -= to_read;
            }

            mprotect(ctx, page_size, PROT_READ | PROT_WRITE);
            memset(ctx, 0, page_size);
            munlock(ctx, page_size);
            munmap(ctx, page_size);

            std::cout << "Extracted " << target << " to " << out_path << "\n";
            return;
        } else {
            in.seekg(file_len, std::ios::cur);
        }
    }
    std::cerr << "File not found in image.\n";
}

enum Action { ADD, LIST, GET, NONE };

int main(int argc, char* argv[]) {
    Action action = NONE;
    std::string out_file, target_file;
    std::vector<std::string> inputs;

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

    if (action == ADD) {
        if (global_key.empty() || inputs.empty()) {
            std::cerr << "Key or input files missing for add operation.\n";
            return 1;
        }
        std::vector<std::string> all_files;
        for (const auto& in_p : inputs) collect_files(in_p, all_files);

        std::size_t base_offset = 0;
        bool image_exists = (access(global_image_path.c_str(), F_OK) == 0);
        if (image_exists) {
            struct stat st;
            if (stat(global_image_path.c_str(), &st) == 0) {
                base_offset = st.st_size;
            }
        }

        std::size_t current_offset = base_offset;
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            for (const auto& f : all_files) {
                std::ifstream tmp(f, std::ios::binary | std::ios::ate);
                std::streamsize data_sz = tmp.tellg();
                if (data_sz < 0) data_sz = 0;

                uint32_t name_len = f.length();
                uint32_t block_sz = 4 + 4 + 16 + name_len + static_cast<uint32_t>(data_sz);

                task_queue.push_back({f, f, current_offset});
                current_offset += block_sz;
            }
        }

        int image_fd = open(global_image_path.c_str(), O_RDWR | O_CREAT, 0644);
        if (image_fd < 0) { std::cerr << "Open failed\n"; return 1; }

        if (ftruncate(image_fd, current_offset) != 0) { std::cerr << "ftruncate failed\n"; return 1; }

        global_image_ptr = static_cast<uint8_t*>(
            mmap(nullptr, current_offset, PROT_READ | PROT_WRITE, MAP_SHARED, image_fd, 0)
        );
        if (global_image_ptr == MAP_FAILED) { std::cerr << "mmap failed\n"; return 1; }
        
        std::vector<std::thread> workers;
        for (int i = 0; i < WORKERS_COUNT; ++i) {
            workers.emplace_back(worker_func);
        }
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            finished_adding = true;
        }
        cv.notify_all();
        for (auto& w : workers) w.join();
        
        msync(global_image_ptr, current_offset, MS_SYNC);
        munmap(global_image_ptr, current_offset);
        close(image_fd);     

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
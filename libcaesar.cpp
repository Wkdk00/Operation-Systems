#include <cstdint>

static char g_key = 0;

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

}
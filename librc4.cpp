#include <cstdint>

extern "C" {

    void rc4_crypt(const uint8_t* key, int key_len, const uint8_t* in, uint8_t* out, int data_len) {
        if (data_len <= 0 || !in || !out || key_len <= 0) return;
        
        uint8_t S[256];
        for (int i = 0; i < 256; i++) S[i] = i;
        
        int j = 0;
        for (int i = 0; i < 256; i++) {
            j = (j + S[i] + key[i % key_len]) % 256;
            uint8_t temp = S[i];
            S[i] = S[j];
            S[j] = temp;
        }
        
        int i = 0; j = 0;
        for (int k = 0; k < data_len; k++) {
            i = (i + 1) % 256;
            j = (j + S[i]) % 256;
            uint8_t temp = S[i];
            S[i] = S[j];
            S[j] = temp;
            uint8_t K = S[(S[i] + S[j]) % 256];
            out[k] = in[k] ^ K;
        }
    }

}
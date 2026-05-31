#include <cstdint>

extern "C" {
    typedef struct {
        uint8_t S[256];
        int i;
        int j;
    } RC4_CTX;

    void rc4_init(RC4_CTX* ctx, const uint8_t* key, int key_len) {
            if (!ctx || !key || key_len <= 0) return;
            
            for (int i = 0; i < 256; i++) ctx->S[i] = i;
            
            ctx->i = 0;
            ctx->j = 0;
            
            int j = 0;
            for (int i = 0; i < 256; i++) {
                j = (j + ctx->S[i] + key[i % key_len]) % 256;
                uint8_t temp = ctx->S[i];
                ctx->S[i] = ctx->S[j];
                ctx->S[j] = temp;
            }
        }

    void rc4_update(RC4_CTX* ctx, const uint8_t* in, uint8_t* out, int data_len) {
            if (!ctx || data_len <= 0 || !in || !out) return;

            int i = ctx->i;
            int j = ctx->j;

            for (int k = 0; k < data_len; k++) {
                i = (i + 1) % 256;
                j = (j + ctx->S[i]) % 256;
                uint8_t temp = ctx->S[i];
                ctx->S[i] = ctx->S[j];
                ctx->S[j] = temp;
                uint8_t K = ctx->S[(ctx->S[i] + ctx->S[j]) % 256];
                out[k] = in[k] ^ K;
            }

            // Сохраняем состояние для следующего чанка
            ctx->i = i;
            ctx->j = j;
        }
}
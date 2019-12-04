#pragma once

#include "pkgi_utils.h"

typedef struct {
    uint32_t key[4*11] GCC_ALIGN(16);
#if __ARM_NEON__
    uint8_t bskey[16*8*9] GCC_ALIGN(16);
#endif
} aes128_ctx;

#define AES_BLOCK_SIZE 16

void aes128_init(aes128_ctx* ctx, const uint8_t* key);
void aes128_encrypt(const aes128_ctx* ctx, const uint8_t* input, uint8_t* output);

void aes128_ctr_init(aes128_ctx* ctx, const uint8_t* key);
void aes128_ctr(const aes128_ctx* ctx, const uint8_t* iv, uint64_t offset, uint8_t* buffer, uint32_t size);

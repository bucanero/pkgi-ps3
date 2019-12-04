#pragma once

#include "pkgi_utils.h"

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

typedef struct {
    uint8_t  buffer[SHA256_BLOCK_SIZE] GCC_ALIGN(16);
    uint32_t state[8];
    uint64_t count;
} sha256_ctx;

void sha256_init(sha256_ctx* ctx);
void sha256_update(sha256_ctx* ctx, const uint8_t* buffer, uint32_t size);
void sha256_finish(sha256_ctx* ctx, uint8_t* digest);

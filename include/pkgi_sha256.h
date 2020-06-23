#pragma once

#include "pkgi_utils.h"
#include <polarssl/sha2.h>

#define SHA256_DIGEST_SIZE 32

#define sha256_ctx			sha2_context
#define sha256_init(ctx)	sha2_starts(ctx, 0)
#define sha256_update		sha2_update
#define sha256_finish		sha2_finish

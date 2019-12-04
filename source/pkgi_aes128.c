#include "pkgi_aes128.h"

#if __ARM_NEON__

// Optimized AES-128 CTR Neon implementation is based on paper from Emilia Kasper and Peter Schwabe:
// "Faster and Timing-Attack Resistant AES-GCM", https://cryptojedi.org/papers/aesbs-20090616.pdf
// Implementation details from public domain qhasm code: https://cryptojedi.org/crypto/index.shtml#aesbs

// It is ~20% faster than C implementation on PlayStation Vita - ~15 MB/s

#include <arm_neon.h>

static inline uint8x16_t vtbl1q_u8(uint8x16_t x, uint8x16_t shuffle)
{
    union
    {
        uint8x16_t  x1;
        uint8x8x2_t x2;
    } u;

    u.x1 = x;
    uint8x8_t low = vtbl2_u8(u.x2, vget_low_u8(shuffle));

    u.x1 = x;
    uint8x8_t high = vtbl2_u8(u.x2, vget_high_u8(shuffle));

    return vcombine_u8(low, high);
}

#define SWAPMOVE(a, b, n, m) do { \
    uint8x16_t t;         \
    t = vshrq_n_u8(b, n); \
    t = veorq_u8(t, a);   \
    t = vandq_u8(t, m);   \
    a = veorq_u8(a, t);   \
    t = vshlq_n_u8(t, n); \
    b = veorq_u8(b, t);   \
} while (0)

#define BITSLICE(x0, x1, x2, x3, x4, x5, x6, x7) do { \
    uint8x16_t m;           \
    m = vdupq_n_u8(0x55);   \
    SWAPMOVE(x0, x1, 1, m); \
    SWAPMOVE(x2, x3, 1, m); \
    SWAPMOVE(x4, x5, 1, m); \
    SWAPMOVE(x6, x7, 1, m); \
    m = vdupq_n_u8(0x33);   \
    SWAPMOVE(x0, x2, 2, m); \
    SWAPMOVE(x1, x3, 2, m); \
    SWAPMOVE(x4, x6, 2, m); \
    SWAPMOVE(x5, x7, 2, m); \
    m = vdupq_n_u8(0x0f);   \
    SWAPMOVE(x0, x4, 4, m); \
    SWAPMOVE(x1, x5, 4, m); \
    SWAPMOVE(x2, x6, 4, m); \
    SWAPMOVE(x3, x7, 4, m); \
} while (0)

#endif

static const uint8_t rcon[] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36,
};

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static const uint32_t sbox32[256] = {
    0xc66363a5, 0xf87c7c84, 0xee777799, 0xf67b7b8d, 0xfff2f20d, 0xd66b6bbd, 0xde6f6fb1, 0x91c5c554,
    0x60303050, 0x02010103, 0xce6767a9, 0x562b2b7d, 0xe7fefe19, 0xb5d7d762, 0x4dababe6, 0xec76769a,
    0x8fcaca45, 0x1f82829d, 0x89c9c940, 0xfa7d7d87, 0xeffafa15, 0xb25959eb, 0x8e4747c9, 0xfbf0f00b,
    0x41adadec, 0xb3d4d467, 0x5fa2a2fd, 0x45afafea, 0x239c9cbf, 0x53a4a4f7, 0xe4727296, 0x9bc0c05b,
    0x75b7b7c2, 0xe1fdfd1c, 0x3d9393ae, 0x4c26266a, 0x6c36365a, 0x7e3f3f41, 0xf5f7f702, 0x83cccc4f,
    0x6834345c, 0x51a5a5f4, 0xd1e5e534, 0xf9f1f108, 0xe2717193, 0xabd8d873, 0x62313153, 0x2a15153f,
    0x0804040c, 0x95c7c752, 0x46232365, 0x9dc3c35e, 0x30181828, 0x379696a1, 0x0a05050f, 0x2f9a9ab5,
    0x0e070709, 0x24121236, 0x1b80809b, 0xdfe2e23d, 0xcdebeb26, 0x4e272769, 0x7fb2b2cd, 0xea75759f,
    0x1209091b, 0x1d83839e, 0x582c2c74, 0x341a1a2e, 0x361b1b2d, 0xdc6e6eb2, 0xb45a5aee, 0x5ba0a0fb,
    0xa45252f6, 0x763b3b4d, 0xb7d6d661, 0x7db3b3ce, 0x5229297b, 0xdde3e33e, 0x5e2f2f71, 0x13848497,
    0xa65353f5, 0xb9d1d168, 0x00000000, 0xc1eded2c, 0x40202060, 0xe3fcfc1f, 0x79b1b1c8, 0xb65b5bed,
    0xd46a6abe, 0x8dcbcb46, 0x67bebed9, 0x7239394b, 0x944a4ade, 0x984c4cd4, 0xb05858e8, 0x85cfcf4a,
    0xbbd0d06b, 0xc5efef2a, 0x4faaaae5, 0xedfbfb16, 0x864343c5, 0x9a4d4dd7, 0x66333355, 0x11858594,
    0x8a4545cf, 0xe9f9f910, 0x04020206, 0xfe7f7f81, 0xa05050f0, 0x783c3c44, 0x259f9fba, 0x4ba8a8e3,
    0xa25151f3, 0x5da3a3fe, 0x804040c0, 0x058f8f8a, 0x3f9292ad, 0x219d9dbc, 0x70383848, 0xf1f5f504,
    0x63bcbcdf, 0x77b6b6c1, 0xafdada75, 0x42212163, 0x20101030, 0xe5ffff1a, 0xfdf3f30e, 0xbfd2d26d,
    0x81cdcd4c, 0x180c0c14, 0x26131335, 0xc3ecec2f, 0xbe5f5fe1, 0x359797a2, 0x884444cc, 0x2e171739,
    0x93c4c457, 0x55a7a7f2, 0xfc7e7e82, 0x7a3d3d47, 0xc86464ac, 0xba5d5de7, 0x3219192b, 0xe6737395,
    0xc06060a0, 0x19818198, 0x9e4f4fd1, 0xa3dcdc7f, 0x44222266, 0x542a2a7e, 0x3b9090ab, 0x0b888883,
    0x8c4646ca, 0xc7eeee29, 0x6bb8b8d3, 0x2814143c, 0xa7dede79, 0xbc5e5ee2, 0x160b0b1d, 0xaddbdb76,
    0xdbe0e03b, 0x64323256, 0x743a3a4e, 0x140a0a1e, 0x924949db, 0x0c06060a, 0x4824246c, 0xb85c5ce4,
    0x9fc2c25d, 0xbdd3d36e, 0x43acacef, 0xc46262a6, 0x399191a8, 0x319595a4, 0xd3e4e437, 0xf279798b,
    0xd5e7e732, 0x8bc8c843, 0x6e373759, 0xda6d6db7, 0x018d8d8c, 0xb1d5d564, 0x9c4e4ed2, 0x49a9a9e0,
    0xd86c6cb4, 0xac5656fa, 0xf3f4f407, 0xcfeaea25, 0xca6565af, 0xf47a7a8e, 0x47aeaee9, 0x10080818,
    0x6fbabad5, 0xf0787888, 0x4a25256f, 0x5c2e2e72, 0x381c1c24, 0x57a6a6f1, 0x73b4b4c7, 0x97c6c651,
    0xcbe8e823, 0xa1dddd7c, 0xe874749c, 0x3e1f1f21, 0x964b4bdd, 0x61bdbddc, 0x0d8b8b86, 0x0f8a8a85,
    0xe0707090, 0x7c3e3e42, 0x71b5b5c4, 0xcc6666aa, 0x904848d8, 0x06030305, 0xf7f6f601, 0x1c0e0e12,
    0xc26161a3, 0x6a35355f, 0xae5757f9, 0x69b9b9d0, 0x17868691, 0x99c1c158, 0x3a1d1d27, 0x279e9eb9,
    0xd9e1e138, 0xebf8f813, 0x2b9898b3, 0x22111133, 0xd26969bb, 0xa9d9d970, 0x078e8e89, 0x339494a7,
    0x2d9b9bb6, 0x3c1e1e22, 0x15878792, 0xc9e9e920, 0x87cece49, 0xaa5555ff, 0x50282878, 0xa5dfdf7a,
    0x038c8c8f, 0x59a1a1f8, 0x09898980, 0x1a0d0d17, 0x65bfbfda, 0xd7e6e631, 0x844242c6, 0xd06868b8,
    0x824141c3, 0x299999b0, 0x5a2d2d77, 0x1e0f0f11, 0x7bb0b0cb, 0xa85454fc, 0x6dbbbbd6, 0x2c16163a,
};

static uint32_t setup_mix(uint32_t temp)
{
    return (sbox[byte32(temp, 2)] << 24) ^ (sbox[byte32(temp, 1)] << 16) ^ (sbox[byte32(temp, 0)] << 8) ^ sbox[byte32(temp, 3)];
}

void aes128_init(aes128_ctx* ctx, const uint8_t* key)
{
    uint32_t* rk = ctx->key;

    rk[0] = get32be(key + 0);
    rk[1] = get32be(key + 4);
    rk[2] = get32be(key + 8);
    rk[3] = get32be(key + 12);

    for (uint32_t i = 0; i < 10; i++)
    {
        rk[4] = rk[0] ^ setup_mix(rk[3]) ^ (rcon[i] << 24);
        rk[5] = rk[1] ^ rk[4];
        rk[6] = rk[2] ^ rk[5];
        rk[7] = rk[3] ^ rk[6];
        rk += 4;
    }
}

void aes128_ctr_init(aes128_ctx* ctx, const uint8_t* key)
{
    aes128_init(ctx, key);

#if __ARM_NEON__
    static const uint8_t M0_bytes[] GCC_ALIGN(16) = { 0xf, 0xb, 0x7, 0x3, 0xe, 0xa, 0x6, 0x2, 0xd, 0x9, 0x5, 0x1, 0xc, 0x8, 0x4, 0x0 };
    uint8x16_t M0 = vld1q_u8(M0_bytes);

    const uint8_t* rk8 = __builtin_assume_aligned(ctx->key, 16);
    uint8_t* bskey = __builtin_assume_aligned(ctx->bskey, 16);

    for (int i=0; i<9; i++)
    {
        uint8x16_t x0 = vrev32q_u8(vld1q_u8(rk8 + 16*(i+1)));
        x0 = vtbl1q_u8(x0, M0);
        uint8x16_t x1 = x0;
        uint8x16_t x2 = x0;
        uint8x16_t x3 = x0;
        uint8x16_t x4 = x0;
        uint8x16_t x5 = x0;
        uint8x16_t x6 = x0;
        uint8x16_t x7 = x0;

        BITSLICE(x7, x6, x5, x4, x3, x2, x1, x0);

        vst1q_u8(bskey + 8*16*i + 0*16, vmvnq_u8(x0));
        vst1q_u8(bskey + 8*16*i + 1*16, vmvnq_u8(x1));
        vst1q_u8(bskey + 8*16*i + 2*16, x2);
        vst1q_u8(bskey + 8*16*i + 3*16, x3);
        vst1q_u8(bskey + 8*16*i + 4*16, x4);
        vst1q_u8(bskey + 8*16*i + 5*16, vmvnq_u8(x5));
        vst1q_u8(bskey + 8*16*i + 6*16, vmvnq_u8(x6));
        vst1q_u8(bskey + 8*16*i + 7*16, x7);
    }
#endif
}

void aes128_encrypt(const aes128_ctx* ctx, const uint8_t* input, uint8_t* output)
{
    const uint32_t* key = ctx->key;

    uint32_t s0 = get32be(input + 0) ^ *key++;
    uint32_t s1 = get32be(input + 4) ^ *key++;
    uint32_t s2 = get32be(input + 8) ^ *key++;
    uint32_t s3 = get32be(input + 12) ^ *key++;

    uint32_t t0 = sbox32[byte32(s0, 3)] ^ ror32(sbox32[byte32(s1, 2)], 8) ^ ror32(sbox32[byte32(s2, 1)], 16) ^ ror32(sbox32[byte32(s3, 0)], 24) ^ *key++;
    uint32_t t1 = sbox32[byte32(s1, 3)] ^ ror32(sbox32[byte32(s2, 2)], 8) ^ ror32(sbox32[byte32(s3, 1)], 16) ^ ror32(sbox32[byte32(s0, 0)], 24) ^ *key++;
    uint32_t t2 = sbox32[byte32(s2, 3)] ^ ror32(sbox32[byte32(s3, 2)], 8) ^ ror32(sbox32[byte32(s0, 1)], 16) ^ ror32(sbox32[byte32(s1, 0)], 24) ^ *key++;
    uint32_t t3 = sbox32[byte32(s3, 3)] ^ ror32(sbox32[byte32(s0, 2)], 8) ^ ror32(sbox32[byte32(s1, 1)], 16) ^ ror32(sbox32[byte32(s2, 0)], 24) ^ *key++;

    for (uint32_t i = 0; i < 4; i++)
    {
        s0 = sbox32[byte32(t0, 3)] ^ ror32(sbox32[byte32(t1, 2)], 8) ^ ror32(sbox32[byte32(t2, 1)], 16) ^ ror32(sbox32[byte32(t3, 0)], 24) ^ *key++;
        s1 = sbox32[byte32(t1, 3)] ^ ror32(sbox32[byte32(t2, 2)], 8) ^ ror32(sbox32[byte32(t3, 1)], 16) ^ ror32(sbox32[byte32(t0, 0)], 24) ^ *key++;
        s2 = sbox32[byte32(t2, 3)] ^ ror32(sbox32[byte32(t3, 2)], 8) ^ ror32(sbox32[byte32(t0, 1)], 16) ^ ror32(sbox32[byte32(t1, 0)], 24) ^ *key++;
        s3 = sbox32[byte32(t3, 3)] ^ ror32(sbox32[byte32(t0, 2)], 8) ^ ror32(sbox32[byte32(t1, 1)], 16) ^ ror32(sbox32[byte32(t2, 0)], 24) ^ *key++;

        t0 = sbox32[byte32(s0, 3)] ^ ror32(sbox32[byte32(s1, 2)], 8) ^ ror32(sbox32[byte32(s2, 1)], 16) ^ ror32(sbox32[byte32(s3, 0)], 24) ^ *key++;
        t1 = sbox32[byte32(s1, 3)] ^ ror32(sbox32[byte32(s2, 2)], 8) ^ ror32(sbox32[byte32(s3, 1)], 16) ^ ror32(sbox32[byte32(s0, 0)], 24) ^ *key++;
        t2 = sbox32[byte32(s2, 3)] ^ ror32(sbox32[byte32(s3, 2)], 8) ^ ror32(sbox32[byte32(s0, 1)], 16) ^ ror32(sbox32[byte32(s1, 0)], 24) ^ *key++;
        t3 = sbox32[byte32(s3, 3)] ^ ror32(sbox32[byte32(s0, 2)], 8) ^ ror32(sbox32[byte32(s1, 1)], 16) ^ ror32(sbox32[byte32(s2, 0)], 24) ^ *key++;
    }

    s0 = (sbox[byte32(t0, 3)] << 24) ^ (sbox[byte32(t1, 2)] << 16) ^ (sbox[byte32(t2, 1)] << 8) ^ (sbox[byte32(t3, 0)]) ^ *key++;
    s1 = (sbox[byte32(t1, 3)] << 24) ^ (sbox[byte32(t2, 2)] << 16) ^ (sbox[byte32(t3, 1)] << 8) ^ (sbox[byte32(t0, 0)]) ^ *key++;
    s2 = (sbox[byte32(t2, 3)] << 24) ^ (sbox[byte32(t3, 2)] << 16) ^ (sbox[byte32(t0, 1)] << 8) ^ (sbox[byte32(t1, 0)]) ^ *key++;
    s3 = (sbox[byte32(t3, 3)] << 24) ^ (sbox[byte32(t0, 2)] << 16) ^ (sbox[byte32(t1, 1)] << 8) ^ (sbox[byte32(t2, 0)]) ^ *key++;

    set32be(output + 0, s0);
    set32be(output + 4, s1);
    set32be(output + 8, s2);
    set32be(output + 12, s3);
}

static void ctr_add(uint8_t* counter, uint64_t n)
{
    for (int i = AES_BLOCK_SIZE-1; n && i >= 0; i--)
    {
        n = n + counter[i];
        counter[i] = (uint8_t)n;
        n >>= 8;
    }
}

static void ctr_inc(uint8_t* counter)
{
    int n = AES_BLOCK_SIZE;
    do {
        if (++counter[--n]) return;
    } while (n);
}

#ifdef __ARM_NEON__

#define Inv_GF256(x0, x1, x2, x3, x4, x5, x6, x7, t0, t1, t2, t3, s0, s1, s2, s3) do { \
    t3 = veorq_u8(x4, x6); \
    t2 = veorq_u8(x5, x7); \
    t1 = veorq_u8(x1, x3); \
    s1 = veorq_u8(x7, x6); \
    s0 = veorq_u8(x0, x2); \
    s2 = t3;               \
    t0 = t2;               \
    s3 = t3;               \
    t2 = vorrq_u8(t2, t1); \
    t3 = vorrq_u8(t3, s0); \
    s3 = veorq_u8(s3, t0); \
    s2 = vandq_u8(s2, s0); \
    t0 = vandq_u8(t0, t1); \
    s0 = veorq_u8(s0, t1); \
    s3 = vandq_u8(s3, s0); \
    s0 = x3;               \
    s0 = veorq_u8(s0, x2); \
    s1 = vandq_u8(s1, s0); \
    t3 = veorq_u8(t3, s1); \
    t2 = veorq_u8(t2, s1); \
    s1 = x4;               \
    s1 = veorq_u8(s1, x5); \
    s0 = x1;               \
    t1 = s1;               \
    s0 = veorq_u8(s0, x0); \
    t1 = vorrq_u8(t1, s0); \
    s1 = vandq_u8(s1, s0); \
    t0 = veorq_u8(t0, s1); \
    t3 = veorq_u8(t3, s3); \
    t2 = veorq_u8(t2, s2); \
    t1 = veorq_u8(t1, s3); \
    t0 = veorq_u8(t0, s2); \
    t1 = veorq_u8(t1, s2); \
    s0 = x7;               \
    s1 = x6;               \
    s2 = x5;               \
    s3 = x4;               \
    s0 = vandq_u8(s0, x3); \
    s1 = vandq_u8(s1, x2); \
    s2 = vandq_u8(s2, x1); \
    s3 = vorrq_u8(s3, x0); \
    t3 = veorq_u8(t3, s0); \
    t2 = veorq_u8(t2, s1); \
    t1 = veorq_u8(t1, s2); \
    t0 = veorq_u8(t0, s3); \
    s0 = t3;               \
    s0 = veorq_u8(s0, t2); \
    t3 = vandq_u8(t3, t1); \
    s2 = t0;               \
    s2 = veorq_u8(s2, t3); \
    s3 = s0;               \
    s3 = vandq_u8(s3, s2); \
    s3 = veorq_u8(s3, t2); \
    s1 = t1;               \
    s1 = veorq_u8(s1, t0); \
    t3 = veorq_u8(t3, t2); \
    s1 = vandq_u8(s1, t3); \
    s1 = veorq_u8(s1, t0); \
    t1 = veorq_u8(t1, s1); \
    t2 = s2;               \
    t2 = veorq_u8(t2, s1); \
    t2 = vandq_u8(t2, t0); \
    t1 = veorq_u8(t1, t2); \
    s2 = veorq_u8(s2, t2); \
    s2 = vandq_u8(s2, s3); \
    s2 = veorq_u8(s2, s0); \
    s0 = x0;               \
    t0 = x1;               \
    t2 = s3;               \
    t2 = veorq_u8(t2, s2); \
    t2 = vandq_u8(t2, x0); \
    x0 = veorq_u8(x0, x1); \
    x0 = vandq_u8(x0, s2); \
    x1 = vandq_u8(x1, s3); \
    x0 = veorq_u8(x0, x1); \
    x1 = veorq_u8(x1, t2); \
    s0 = veorq_u8(s0, x2); \
    t0 = veorq_u8(t0, x3); \
    s3 = veorq_u8(s3, s1); \
    s2 = veorq_u8(s2, t1); \
    t3 = s3;               \
    t3 = veorq_u8(t3, s2); \
    t3 = vandq_u8(t3, s0); \
    s0 = veorq_u8(s0, t0); \
    s0 = vandq_u8(s0, s2); \
    t0 = vandq_u8(t0, s3); \
    t0 = veorq_u8(t0, s0); \
    s0 = veorq_u8(s0, t3); \
    t2 = s1;               \
    t2 = veorq_u8(t2, t1); \
    t2 = vandq_u8(t2, x2); \
    x2 = veorq_u8(x2, x3); \
    x2 = vandq_u8(x2, t1); \
    x3 = vandq_u8(x3, s1); \
    x2 = veorq_u8(x2, x3); \
    x3 = veorq_u8(x3, t2); \
    x0 = veorq_u8(x0, s0); \
    x2 = veorq_u8(x2, s0); \
    x1 = veorq_u8(x1, t0); \
    x3 = veorq_u8(x3, t0); \
    s0 = x4;               \
    t0 = x5;               \
    s0 = veorq_u8(s0, x6); \
    t0 = veorq_u8(t0, x7); \
    t3 = s3;               \
    t3 = veorq_u8(t3, s2); \
    t3 = vandq_u8(t3, s0); \
    s0 = veorq_u8(s0, t0); \
    s0 = vandq_u8(s0, s2); \
    t0 = vandq_u8(t0, s3); \
    t0 = veorq_u8(t0, s0); \
    s0 = veorq_u8(s0, t3); \
    t2 = s1;               \
    t2 = veorq_u8(t2, t1); \
    t2 = vandq_u8(t2, x6); \
    x6 = veorq_u8(x6, x7); \
    x6 = vandq_u8(x6, t1); \
    x7 = vandq_u8(x7, s1); \
    x6 = veorq_u8(x6, x7); \
    x7 = veorq_u8(x7, t2); \
    s3 = veorq_u8(s3, s1); \
    s2 = veorq_u8(s2, t1); \
    t3 = s3;               \
    t3 = veorq_u8(t3, s2); \
    t3 = vandq_u8(t3, x4); \
    x4 = veorq_u8(x4, x5); \
    x4 = vandq_u8(x4, s2); \
    x5 = vandq_u8(x5, s3); \
    x4 = veorq_u8(x4, x5); \
    x5 = veorq_u8(x5, t3); \
    x4 = veorq_u8(x4, s0); \
    x6 = veorq_u8(x6, s0); \
    x5 = veorq_u8(x5, t0); \
    x7 = veorq_u8(x7, t0); \
} while (0)

#define InBasisChange(b0, b1, b2, b3, b4, b5, b6, b7) do { \
    b5 = veorq_u8(b5, b6); \
    b2 = veorq_u8(b2, b1); \
    b5 = veorq_u8(b5, b0); \
    b6 = veorq_u8(b6, b2); \
    b3 = veorq_u8(b3, b0); \
    b6 = veorq_u8(b6, b3); \
    b3 = veorq_u8(b3, b7); \
    b3 = veorq_u8(b3, b4); \
    b7 = veorq_u8(b7, b5); \
    b3 = veorq_u8(b3, b1); \
    b4 = veorq_u8(b4, b5); \
    b2 = veorq_u8(b2, b7); \
    b1 = veorq_u8(b1, b5); \
} while (0)
 
#define OutBasisChange(b0, b1, b2, b3, b4, b5, b6, b7) do { \
    b0 = veorq_u8(b0, b6); \
    b1 = veorq_u8(b1, b4); \
    b2 = veorq_u8(b2, b0); \
    b4 = veorq_u8(b4, b6); \
    b6 = veorq_u8(b6, b1); \
    b1 = veorq_u8(b1, b5); \
    b5 = veorq_u8(b5, b3); \
    b2 = veorq_u8(b2, b5); \
    b3 = veorq_u8(b3, b7); \
    b7 = veorq_u8(b7, b5); \
    b4 = veorq_u8(b4, b7); \
} while (0)

// GCC is bad at optimizing this :(
#define SBOX_noinlineasm(b0, b1, b2, b3, b4, b5, b6, b7, t0, t1, t2, t3, t4, t5, t6, t7) do { \
    InBasisChange(b0, b1, b2, b3, b4, b5, b6, b7); \
    Inv_GF256(b6, b5, b0, b3, b7, b1, b4, b2, t0, t1, t2, t3, t4, t5, t6, t7); \
    OutBasisChange(b7, b1, b4, b2, b6, b5, b0, b3); \
} while (0)

#define SBOX(b0, b1, b2, b3, b4, b5, b6, b7, t0, t1, t2, t3, t4, t5, t6, t7) \
    __asm__ __volatile__( \
        "veor %q5, %q5, %q6    \n\t" \
        "veor %q2, %q2, %q1    \n\t" \
        "veor %q5, %q5, %q0    \n\t" \
        "veor %q6, %q6, %q2    \n\t" \
        "veor %q3, %q3, %q0    \n\t" \
        "veor %q6, %q6, %q3    \n\t" \
        "veor %q3, %q3, %q7    \n\t" \
        "veor %q3, %q3, %q4    \n\t" \
        "veor %q7, %q7, %q5    \n\t" \
        "veor %q3, %q3, %q1    \n\t" \
        "veor %q4, %q4, %q5    \n\t" \
        "veor %q2, %q2, %q7    \n\t" \
        "veor %q1, %q1, %q5    \n\t" \
        \
        "veor %q11, %q7, %q4   \n\t" \
        "veor %q10, %q1, %q2   \n\t" \
        "veor %q9, %q5, %q3    \n\t" \
        "veor %q13, %q2, %q4   \n\t" \
        "veor %q12, %q6, %q0   \n\t" \
        "vmov %q14, %q11       \n\t" \
        "vmov %q8, %q10        \n\t" \
        "vmov %q15, %q11       \n\t" \
        "vorr %q10, %q10, %q9  \n\t" \
        "vorr %q11, %q11, %q12 \n\t" \
        "veor %q15, %q15, %q8  \n\t" \
        "vand %q14, %q14, %q12 \n\t" \
        "vand %q8, %q8, %q9    \n\t" \
        "veor %q12, %q12, %q9  \n\t" \
        "vand %q15, %q15, %q12 \n\t" \
        "vmov %q12, %q3        \n\t" \
        "veor %q12, %q12, %q0  \n\t" \
        "vand %q13, %q13, %q12 \n\t" \
        "veor %q11, %q11, %q13 \n\t" \
        "veor %q10, %q10, %q13 \n\t" \
        "vmov %q13, %q7        \n\t" \
        "veor %q13, %q13, %q1  \n\t" \
        "vmov %q12, %q5        \n\t" \
        "vmov %q9, %q13        \n\t" \
        "veor %q12, %q12, %q6  \n\t" \
        "vorr %q9, %q9, %q12   \n\t" \
        "vand %q13, %q13, %q12 \n\t" \
        "veor %q8, %q8, %q13   \n\t" \
        "veor %q11, %q11, %q15 \n\t" \
        "veor %q10, %q10, %q14 \n\t" \
        "veor %q9, %q9, %q15   \n\t" \
        "veor %q8, %q8, %q14   \n\t" \
        "veor %q9, %q9, %q14   \n\t" \
        "vmov %q12, %q2        \n\t" \
        "vmov %q13, %q4        \n\t" \
        "vmov %q14, %q1        \n\t" \
        "vmov %q15, %q7        \n\t" \
        "vand %q12, %q12, %q3  \n\t" \
        "vand %q13, %q13, %q0  \n\t" \
        "vand %q14, %q14, %q5  \n\t" \
        "vorr %q15, %q15, %q6  \n\t" \
        "veor %q11, %q11, %q12 \n\t" \
        "veor %q10, %q10, %q13 \n\t" \
        "veor %q9, %q9, %q14   \n\t" \
        "veor %q8, %q8, %q15   \n\t" \
        "vmov %q12, %q11       \n\t" \
        "veor %q12, %q12, %q10 \n\t" \
        "vand %q11, %q11, %q9  \n\t" \
        "vmov %q14, %q8        \n\t" \
        "veor %q14, %q14, %q11 \n\t" \
        "vmov %q15, %q12       \n\t" \
        "vand %q15, %q15, %q14 \n\t" \
        "veor %q15, %q15, %q10 \n\t" \
        "vmov %q13, %q9        \n\t" \
        "veor %q13, %q13, %q8  \n\t" \
        "veor %q11, %q11, %q10 \n\t" \
        "vand %q13, %q13, %q11 \n\t" \
        "veor %q13, %q13, %q8  \n\t" \
        "veor %q9, %q9, %q13   \n\t" \
        "vmov %q10, %q14       \n\t" \
        "veor %q10, %q10, %q13 \n\t" \
        "vand %q10, %q10, %q8  \n\t" \
        "veor %q9, %q9, %q10   \n\t" \
        "veor %q14, %q14, %q10 \n\t" \
        "vand %q14, %q14, %q15 \n\t" \
        "veor %q14, %q14, %q12 \n\t" \
        "vmov %q12, %q6        \n\t" \
        "vmov %q8, %q5         \n\t" \
        "vmov %q10, %q15       \n\t" \
        "veor %q10, %q10, %q14 \n\t" \
        "vand %q10, %q10, %q6  \n\t" \
        "veor %q6, %q6, %q5    \n\t" \
        "vand %q6, %q6, %q14   \n\t" \
        "vand %q5, %q5, %q15   \n\t" \
        "veor %q6, %q6, %q5    \n\t" \
        "veor %q5, %q5, %q10   \n\t" \
        "veor %q12, %q12, %q0  \n\t" \
        "veor %q8, %q8, %q3    \n\t" \
        "veor %q15, %q15, %q13 \n\t" \
        "veor %q14, %q14, %q9  \n\t" \
        "vmov %q11, %q15       \n\t" \
        "veor %q11, %q11, %q14 \n\t" \
        "vand %q11, %q11, %q12 \n\t" \
        "veor %q12, %q12, %q8  \n\t" \
        "vand %q12, %q12, %q14 \n\t" \
        "vand %q8, %q8, %q15   \n\t" \
        "veor %q8, %q8, %q12   \n\t" \
        "veor %q12, %q12, %q11 \n\t" \
        "vmov %q10, %q13       \n\t" \
        "veor %q10, %q10, %q9  \n\t" \
        "vand %q10, %q10, %q0  \n\t" \
        "veor %q0, %q0, %q3    \n\t" \
        "vand %q0, %q0, %q9    \n\t" \
        "vand %q3, %q3, %q13   \n\t" \
        "veor %q0, %q0, %q3    \n\t" \
        "veor %q3, %q3, %q10   \n\t" \
        "veor %q6, %q6, %q12   \n\t" \
        "veor %q0, %q0, %q12   \n\t" \
        "veor %q5, %q5, %q8    \n\t" \
        "veor %q3, %q3, %q8    \n\t" \
        "vmov %q12, %q7        \n\t" \
        "vmov %q8, %q1         \n\t" \
        "veor %q12, %q12, %q4  \n\t" \
        "veor %q8, %q8, %q2    \n\t" \
        "vmov %q11, %q15       \n\t" \
        "veor %q11, %q11, %q14 \n\t" \
        "vand %q11, %q11, %q12 \n\t" \
        "veor %q12, %q12, %q8  \n\t" \
        "vand %q12, %q12, %q14 \n\t" \
        "vand %q8, %q8, %q15   \n\t" \
        "veor %q8, %q8, %q12   \n\t" \
        "veor %q12, %q12, %q11 \n\t" \
        "vmov %q10, %q13       \n\t" \
        "veor %q10, %q10, %q9  \n\t" \
        "vand %q10, %q10, %q4  \n\t" \
        "veor %q4, %q4, %q2    \n\t" \
        "vand %q4, %q4, %q9    \n\t" \
        "vand %q2, %q2, %q13   \n\t" \
        "veor %q4, %q4, %q2    \n\t" \
        "veor %q2, %q2, %q10   \n\t" \
        "veor %q15, %q15, %q13 \n\t" \
        "veor %q14, %q14, %q9  \n\t" \
        "vmov %q11, %q15       \n\t" \
        "veor %q11, %q11, %q14 \n\t" \
        "vand %q11, %q11, %q7  \n\t" \
        "veor %q7, %q7, %q1    \n\t" \
        "vand %q7, %q7, %q14   \n\t" \
        "vand %q1, %q1, %q15   \n\t" \
        "veor %q7, %q7, %q1    \n\t" \
        "veor %q1, %q1, %q11   \n\t" \
        "veor %q7, %q7, %q12   \n\t" \
        "veor %q4, %q4, %q12   \n\t" \
        "veor %q1, %q1, %q8    \n\t" \
        "veor %q2, %q2, %q8    \n\t" \
        \
        "veor %q7, %q7, %q0    \n\t" \
        "veor %q1, %q1, %q6    \n\t" \
        "veor %q4, %q4, %q7    \n\t" \
        "veor %q6, %q6, %q0    \n\t" \
        "veor %q0, %q0, %q1    \n\t" \
        "veor %q1, %q1, %q5    \n\t" \
        "veor %q5, %q5, %q2    \n\t" \
        "veor %q4, %q4, %q5    \n\t" \
        "veor %q2, %q2, %q3    \n\t" \
        "veor %q3, %q3, %q5    \n\t" \
        "veor %q6, %q6, %q3    \n\t" \
        : "+w"(b0), "+w"(b1), "+w"(b2), "+w"(b3), "+w"(b4), "+w"(b5), "+w"(b6), "+w"(b7) \
        , "=w"(t0), "=w"(t1), "=w"(t2), "=w"(t3), "=w"(t4), "=w"(t5), "=w"(t6), "=w"(t7) \
    )

#define SHIFTROWS(x0, x1, x2, x3, x4, x5, x6, x7, shuffle, bskey) do { \
    x0 = veorq_u8(x0, vld1q_u8(bskey + 0*16)); \
    x1 = veorq_u8(x1, vld1q_u8(bskey + 1*16)); \
    x2 = veorq_u8(x2, vld1q_u8(bskey + 2*16)); \
    x3 = veorq_u8(x3, vld1q_u8(bskey + 3*16)); \
    x4 = veorq_u8(x4, vld1q_u8(bskey + 4*16)); \
    x5 = veorq_u8(x5, vld1q_u8(bskey + 5*16)); \
    x6 = veorq_u8(x6, vld1q_u8(bskey + 6*16)); \
    x7 = veorq_u8(x7, vld1q_u8(bskey + 7*16)); \
    x0 = vtbl1q_u8(x0, shuffle);               \
    x1 = vtbl1q_u8(x1, shuffle);               \
    x2 = vtbl1q_u8(x2, shuffle);               \
    x3 = vtbl1q_u8(x3, shuffle);               \
    x4 = vtbl1q_u8(x4, shuffle);               \
    x5 = vtbl1q_u8(x5, shuffle);               \
    x6 = vtbl1q_u8(x6, shuffle);               \
    x7 = vtbl1q_u8(x7, shuffle);               \
} while (0)

// t = vextq_u8(x, x, 12)  [3, 2, 1, 0] => [2, 1, 0, 3]
// t = vextq_u8(x, x, 8)   [3, 2, 1, 0] => [1, 0, 3, 2]
#define MIXCOLUMNS(x0, x1, x2, x3, x4, x5, x6, x7, t0, t1, t2, t3, t4, t5, t6, t7) do { \
    t0 = vextq_u8(x0, x0, 12); \
    t1 = vextq_u8(x1, x1, 12); \
    t2 = vextq_u8(x2, x2, 12); \
    t3 = vextq_u8(x3, x3, 12); \
    t4 = vextq_u8(x4, x4, 12); \
    t5 = vextq_u8(x5, x5, 12); \
    t6 = vextq_u8(x6, x6, 12); \
    t7 = vextq_u8(x7, x7, 12); \
    x0 = veorq_u8(x0, t0);     \
    x1 = veorq_u8(x1, t1);     \
    x2 = veorq_u8(x2, t2);     \
    x3 = veorq_u8(x3, t3);     \
    x4 = veorq_u8(x4, t4);     \
    x5 = veorq_u8(x5, t5);     \
    x6 = veorq_u8(x6, t6);     \
    x7 = veorq_u8(x7, t7);     \
    t0 = veorq_u8(t0, x7);     \
    t1 = veorq_u8(t1, x0);     \
    t2 = veorq_u8(t2, x1);     \
    t1 = veorq_u8(t1, x7);     \
    t3 = veorq_u8(t3, x2);     \
    t4 = veorq_u8(t4, x3);     \
    t5 = veorq_u8(t5, x4);     \
    t3 = veorq_u8(t3, x7);     \
    t6 = veorq_u8(t6, x5);     \
    t7 = veorq_u8(t7, x6);     \
    t4 = veorq_u8(t4, x7);     \
    x0 = vextq_u8(x0, x0, 8);  \
    x1 = vextq_u8(x1, x1, 8);  \
    x2 = vextq_u8(x2, x2, 8);  \
    x3 = vextq_u8(x3, x3, 8);  \
    x4 = vextq_u8(x4, x4, 8);  \
    x5 = vextq_u8(x5, x5, 8);  \
    x6 = vextq_u8(x6, x6, 8);  \
    x7 = vextq_u8(x7, x7, 8);  \
    x0 = veorq_u8(x0, t0);     \
    x1 = veorq_u8(x1, t1);     \
    t2 = veorq_u8(t2, x2);     \
    t3 = veorq_u8(t3, x3);     \
    t4 = veorq_u8(t4, x4);     \
    t5 = veorq_u8(t5, x5);     \
    t6 = veorq_u8(t6, x6);     \
    t7 = veorq_u8(t7, x7);     \
    x6 = t2;                   \
    x4 = t3;                   \
    x2 = t4;                   \
    x7 = t5;                   \
    x3 = t6;                   \
    x5 = t7;                   \
} while (0)   

static void aes128_ctr_neon(const aes128_ctx* ctx, uint8_t* iv, uint8_t* buffer, uint32_t blocks)
{
    static const uint32_t one_bytes[] GCC_ALIGN(16) = { 0, 0, 0, 1 };
    uint32x4_t ctr = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(iv)));

    while (blocks != 0)
    {
        uint32x4_t one = vld1q_u32(one_bytes);

        uint8x16_t x0 = vrev32q_u8(vreinterpretq_u8_u32(ctr)); ctr = vaddq_u32(ctr, one);
        uint8x16_t x1 = vrev32q_u8(vreinterpretq_u8_u32(ctr)); ctr = vaddq_u32(ctr, one);
        uint8x16_t x2 = vrev32q_u8(vreinterpretq_u8_u32(ctr)); ctr = vaddq_u32(ctr, one);
        uint8x16_t x3 = vrev32q_u8(vreinterpretq_u8_u32(ctr)); ctr = vaddq_u32(ctr, one);
        uint8x16_t x4 = vrev32q_u8(vreinterpretq_u8_u32(ctr)); ctr = vaddq_u32(ctr, one);
        uint8x16_t x5 = vrev32q_u8(vreinterpretq_u8_u32(ctr)); ctr = vaddq_u32(ctr, one);
        uint8x16_t x6 = vrev32q_u8(vreinterpretq_u8_u32(ctr)); ctr = vaddq_u32(ctr, one);
        uint8x16_t x7 = vrev32q_u8(vreinterpretq_u8_u32(ctr)); ctr = vaddq_u32(ctr, one);

        // round 0 - addkey & shiftrows
        {
            const uint8_t* key0 = __builtin_assume_aligned(ctx->key, 16);
            uint8x16_t first = vrev32q_u8(vld1q_u8(key0));
            x0 = veorq_u8(x0, first);
            x1 = veorq_u8(x1, first);
            x2 = veorq_u8(x2, first);
            x3 = veorq_u8(x3, first);
            x4 = veorq_u8(x4, first);
            x5 = veorq_u8(x5, first);
            x6 = veorq_u8(x6, first);
            x7 = veorq_u8(x7, first);

            static const uint8_t M0SR_bytes[] GCC_ALIGN(16) = { 0xb, 0x7, 0x3, 0xf, 0x6, 0x2, 0xe, 0xa, 0x1, 0xd, 0x9, 0x5, 0xc, 0x8, 0x4, 0x0 };
            uint8x16_t M0SR = vld1q_u8(M0SR_bytes);

            x0 = vtbl1q_u8(x0, M0SR);
            x1 = vtbl1q_u8(x1, M0SR);
            x2 = vtbl1q_u8(x2, M0SR);
            x3 = vtbl1q_u8(x3, M0SR);
            x4 = vtbl1q_u8(x4, M0SR);
            x5 = vtbl1q_u8(x5, M0SR);
            x6 = vtbl1q_u8(x6, M0SR);
            x7 = vtbl1q_u8(x7, M0SR);
        }

        BITSLICE(x7, x6, x5, x4, x3, x2, x1, x0);

        static const uint8_t SR_bytes[] GCC_ALIGN(16) = { 0x1, 0x2, 0x3, 0x0, 0x6, 0x7, 0x4, 0x5, 0xb, 0x8, 0x9, 0xa, 0xc, 0xd, 0xe, 0xf };
        uint8x16_t shuffle = vld1q_u8(SR_bytes);

        int round = 9;
        const uint8_t* bskey = __builtin_assume_aligned(ctx->bskey, 16);

        // rounds [1..9]
        {
            uint8x16_t t0, t1, t2, t3, t4, t5, t6, t7;
            goto entry;
loop:
            // for rounds [2..9] - addkey & shiftrows
            SHIFTROWS(x0, x1, x2, x3, x4, x5, x6, x7, shuffle, bskey);
            bskey += 8*16;
entry:
            // for rounds [1..9] - sbox
            SBOX(x0, x1, x2, x3, x4, x5, x6, x7, t0, t1, t2, t3, t4, t5, t6, t7);
            --round;
            if (round < 0)
            {
                goto done;
            }
            else if (round == 0)
            {
                // round 9 skips mixcolumns
                static const uint8_t SRM0_bytes[] GCC_ALIGN(16) = { 0xf, 0xa, 0x5, 0x0, 0xe, 0x9, 0x4, 0x3, 0xd, 0x8, 0x7, 0x2, 0xc, 0xb, 0x6, 0x1 };
                shuffle = vld1q_u8(SRM0_bytes);
            }  
            // for rounds [0..8] - mixcolumns
            MIXCOLUMNS(x0, x1, x4, x6, x3, x7, x2, x5, t0, t1, t2, t3, t4, t5, t6, t7);
            goto loop;
        }
done:

        BITSLICE(x5, x2, x7, x3, x6, x4, x1, x0);

        // round 10 - addkey
        {
            const uint8_t* key10 = __builtin_assume_aligned(ctx->key + 4*10, 16);
            uint8x16_t last = vrev32q_u8(vld1q_u8(key10));
            last = veorq_u8((last), vdupq_n_u8(0x63));
            x0 = veorq_u8(x0, last);
            x1 = veorq_u8(x1, last);
            x2 = veorq_u8(x2, last);
            x3 = veorq_u8(x3, last);
            x4 = veorq_u8(x4, last);
            x5 = veorq_u8(x5, last);
            x6 = veorq_u8(x6, last);
            x7 = veorq_u8(x7, last);
        }

        x0 = veorq_u8(x0, vld1q_u8(buffer + 16*0));
        x1 = veorq_u8(x1, vld1q_u8(buffer + 16*1));
        x4 = veorq_u8(x4, vld1q_u8(buffer + 16*2));
        x6 = veorq_u8(x6, vld1q_u8(buffer + 16*3));
        x3 = veorq_u8(x3, vld1q_u8(buffer + 16*4));
        x7 = veorq_u8(x7, vld1q_u8(buffer + 16*5));
        x2 = veorq_u8(x2, vld1q_u8(buffer + 16*6));
        x5 = veorq_u8(x5, vld1q_u8(buffer + 16*7));

        vst1q_u8(buffer + 16*0, x0);
        vst1q_u8(buffer + 16*1, x1);
        vst1q_u8(buffer + 16*2, x4);
        vst1q_u8(buffer + 16*3, x6);
        vst1q_u8(buffer + 16*4, x3);
        vst1q_u8(buffer + 16*5, x7);
        vst1q_u8(buffer + 16*6, x2);
        vst1q_u8(buffer + 16*7, x5);

        blocks -= 8;
        buffer += 16 * 8;
    }

    vst1q_u8(iv, vrev32q_u8(vreinterpretq_u8_u32(ctr)));
}

#endif

void aes128_ctr(const aes128_ctx* ctx, const uint8_t* iv, uint64_t offset, uint8_t* buffer, uint32_t size)
{
    uint8_t tmp[AES_BLOCK_SIZE];
    uint8_t counter[AES_BLOCK_SIZE];
    for (uint32_t i = 0; i < AES_BLOCK_SIZE; i++)
    {
        counter[i] = iv[i];
    }
    ctr_add(counter, offset / AES_BLOCK_SIZE);

    uint32_t prefix = offset % AES_BLOCK_SIZE;
    if (prefix != 0)
    {
        aes128_encrypt(ctx, counter, tmp);
        uint32_t count = min32(size, AES_BLOCK_SIZE - prefix);
        for (uint32_t i = 0; i < count; i++)
        {
            *buffer++ ^= tmp[prefix + i];
        }
        ctr_inc(counter);
        size -= count;
    }

#if __ARM_NEON__
    uint32_t blocks = size / AES_BLOCK_SIZE;
    if (blocks >= 8)
    {
        uint32_t full = blocks & ~7;
        aes128_ctr_neon(ctx, counter, buffer, full);
        buffer += full * AES_BLOCK_SIZE;
        size -= full * AES_BLOCK_SIZE;
    }
#endif

    while (size >= AES_BLOCK_SIZE)
    {
        aes128_encrypt(ctx, counter, tmp);
        for (uint32_t i = 0; i < AES_BLOCK_SIZE; i++)
        {
            *buffer++ ^= tmp[i];
        }
        ctr_inc(counter);
        size -= AES_BLOCK_SIZE;
    }

    if (size != 0)
    {
        aes128_encrypt(ctx, counter, tmp);
        for (uint32_t i = 0; i < size; i++)
        {
            *buffer++ ^= tmp[i];
        }
    }
}

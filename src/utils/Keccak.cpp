#include "Keccak.h"

#include <stdint.h>
#include <string.h>

namespace tradeboy::utils {

static inline uint64_t rol64(uint64_t x, unsigned int s) {
    return (x << s) | (x >> (64 - s));
}

static const uint64_t keccakf_rndc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL
};

static const unsigned int keccakf_rotc[24] = {
     1,  3,  6, 10, 15, 21, 28, 36, 45, 55,  2, 14,
    27, 41, 56,  8, 25, 43, 62, 18, 39, 61, 20, 44
};

static const unsigned int keccakf_piln[24] = {
    10,  7, 11, 17, 18,  3,  5, 16,  8, 21, 24,  4,
    15, 23, 19, 13, 12,  2, 20, 14, 22,  9,  6,  1
};

static void keccakf(uint64_t st[25]) {
    uint64_t bc[5];
    for (int round = 0; round < 24; round++) {
        for (int i = 0; i < 5; i++) {
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        }
        for (int i = 0; i < 5; i++) {
            uint64_t t = bc[(i + 4) % 5] ^ rol64(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5) {
                st[j + i] ^= t;
            }
        }

        uint64_t t = st[1];
        for (int i = 0; i < 24; i++) {
            int j = (int)keccakf_piln[i];
            bc[0] = st[j];
            st[j] = rol64(t, keccakf_rotc[i]);
            t = bc[0];
        }

        for (int j = 0; j < 25; j += 5) {
            for (int i = 0; i < 5; i++) bc[i] = st[j + i];
            for (int i = 0; i < 5; i++) {
                st[j + i] = bc[i] ^ ((~bc[(i + 1) % 5]) & bc[(i + 2) % 5]);
            }
        }

        st[0] ^= keccakf_rndc[round];
    }
}

static inline uint64_t load64_le(const unsigned char* p) {
    return ((uint64_t)p[0]) |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static inline void store64_le(unsigned char* p, uint64_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
    p[4] = (unsigned char)((v >> 32) & 0xFF);
    p[5] = (unsigned char)((v >> 40) & 0xFF);
    p[6] = (unsigned char)((v >> 48) & 0xFF);
    p[7] = (unsigned char)((v >> 56) & 0xFF);
}

// Keccak-256 (rate=1088 bits, capacity=512 bits), with Ethereum padding (0x01 .. 0x80)
void keccak_256(const void* data, size_t len, unsigned char out32[32]) {
    uint64_t st[25];
    memset(st, 0, sizeof(st));

    const unsigned char* in = (const unsigned char*)data;
    const size_t rate = 136;

    while (len >= rate) {
        for (size_t i = 0; i < rate / 8; i++) {
            st[i] ^= load64_le(in + i * 8);
        }
        keccakf(st);
        in += rate;
        len -= rate;
    }

    unsigned char temp[rate];
    memset(temp, 0, sizeof(temp));
    if (len > 0) memcpy(temp, in, len);
    temp[len] = 0x01;
    temp[rate - 1] |= 0x80;

    for (size_t i = 0; i < rate / 8; i++) {
        st[i] ^= load64_le(temp + i * 8);
    }
    keccakf(st);

    unsigned char out[rate];
    for (size_t i = 0; i < rate / 8; i++) {
        store64_le(out + i * 8, st[i]);
    }
    memcpy(out32, out, 32);
}

} // namespace tradeboy::utils

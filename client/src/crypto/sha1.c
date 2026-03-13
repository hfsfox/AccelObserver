/*
 * crypto/sha1.c
 * SHA-1 (RFC 3174).
 */
#include <crypto/sha1.h>
#include <string.h>
#include <stdlib.h>

uint32_t rotl32(uint32_t v, unsigned n);
uint32_t load_be32(const uint8_t* p);
void store_be32(uint8_t* p, uint32_t v);

/* Circular left rotate 32-bit word */
uint32_t rotl32(uint32_t v, unsigned n)
{
    return (v << n) | (v >> (32u - n));
}

/* conv big-endian 4 bytes -> uint32 */
uint32_t load_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u)
         | ((uint32_t)p[2] <<  8u) |  (uint32_t)p[3];
}

/* write uint32 in big-endian 4 bytes */
void store_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24u);
    p[1] = (uint8_t)(v >> 16u);
    p[2] = (uint8_t)(v >>  8u);
    p[3] = (uint8_t)(v);
}

/* --------------------------------------------------------------------------- */
void
sha1_compute(const uint8_t* data, size_t len, uint8_t out[20])
{
    uint32_t h[5] =
    {
        0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u
    };

    size_t padded_len = ((len + 8u) / 64u + 1u) * 64u;
    uint8_t* padded = (uint8_t*)calloc(padded_len, 1u);
    if (!padded) return;

    memcpy(padded, data, len);
    padded[len] = 0x80u;

    uint64_t bit_len = (uint64_t)len * 8u;
    for (int i = 7; i >= 0; --i) {
        padded[padded_len - 8u + (size_t)(7 - i)] =
            (uint8_t)(bit_len >> ((unsigned)i * 8u));
    }

    for (size_t blk = 0; blk < padded_len; blk += 64u) {
        uint32_t W[80];
        uint32_t a, b, c, d, e;

        for (int t = 0; t < 16; ++t)
            W[t] = load_be32(padded + blk + (size_t)t * 4u);

        for (int t = 16; t < 80; ++t)
            W[t] = rotl32(W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16], 1u);

        a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];

        for (int t = 0; t < 80; ++t) {
            uint32_t f, k;
            if      (t < 20) { f = (b & c) | (~b & d);              k = 0x5A827999u; }
            else if (t < 40) { f = b ^ c ^ d;                        k = 0x6ED9EBA1u; }
            else if (t < 60) { f = (b & c) | (b & d) | (c & d);     k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                        k = 0xCA62C1D6u; }

            uint32_t tmp = rotl32(a, 5u) + f + e + k + W[t];
            e = d; d = c; c = rotl32(b, 30u); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    free(padded);

    for (int i = 0; i < 5; ++i)
        store_be32(out + (size_t)i * 4u, h[i]);
}

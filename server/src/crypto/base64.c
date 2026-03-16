#include <crypto/base64.h>

static const char B64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

size_t
base64_encode(const uint8_t* data, size_t len, char* out)
{
    size_t out_pos = 0;
    for (size_t i = 0; i < len; i += 3u) {
        uint32_t b  = (uint32_t)data[i] << 16u;
        if (i + 1u < len) b |= (uint32_t)data[i + 1u] << 8u;
        if (i + 2u < len) b |= (uint32_t)data[i + 2u];

        out[out_pos++] = B64_TABLE[(b >> 18u) & 0x3Fu];
        out[out_pos++] = B64_TABLE[(b >> 12u) & 0x3Fu];
        out[out_pos++] = (i + 1u < len) ? B64_TABLE[(b >> 6u) & 0x3Fu] : '=';
        out[out_pos++] = (i + 2u < len) ? B64_TABLE[(b      ) & 0x3Fu] : '=';
    }
    out[out_pos] = '\0';
    return out_pos;
}

size_t base64_encoded_size(size_t len)
{
    return ((len + 2u) / 3u) * 4u;
}

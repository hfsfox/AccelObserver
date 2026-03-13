#ifndef __WS_CLIENT_SHA1_H__
#define __WS_CLIENT_SHA1_H__

 // SHA-1 (RFC 3174)

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * data     - data input
 * len      - data let in bytes
 * out[20]  - result (20 bytes, big-endian)
 */
void sha1_compute(const uint8_t* data, size_t len, uint8_t out[20]);

#ifdef __cplusplus
}
#endif

#endif /* WS_CLIENT_SHA1_H */

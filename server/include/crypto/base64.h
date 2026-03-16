#ifndef __WS_CLIENT_BASE64_H__
#define __WS_CLIENT_BASE64_H__

//Base64 encode/decode (RFC 4648 §4)
 
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t base64_encode(const uint8_t* data, size_t len, char* out);

size_t base64_encoded_size(size_t len);

#ifdef __cplusplus
}
#endif

#endif

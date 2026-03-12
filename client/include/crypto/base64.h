#ifndef WS_CLIENT_BASE64_H
#define WS_CLIENT_BASE64_H
/* =============================================================================
 * crypto/base64.h
 * Base64 encode/decode (RFC 4648 §4). Нет внешних зависимостей.
 * ============================================================================= */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Кодировать data[len] → Base64 строка с нулём.
 * out должен иметь размер не менее base64_encoded_size(len)+1.
 * Возвращает длину строки (без нуля). */
size_t base64_encode(const uint8_t* data, size_t len, char* out);

/* Необходимый размер буфера для кодирования len байт (без нуля). */
static inline size_t base64_encoded_size(size_t len) {
    return ((len + 2u) / 3u) * 4u;
}

#ifdef __cplusplus
}
#endif

#endif /* WS_CLIENT_BASE64_H */

#ifndef WS_CLIENT_SHA1_H
#define WS_CLIENT_SHA1_H
/* =============================================================================
 * crypto/sha1.h
 * SHA-1 (RFC 3174). Нет зависимостей кроме <stdint.h> и <stddef.h>.
 * ============================================================================= */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Вычислить SHA-1 дайджест.
 * data     — входные данные
 * len      — длина в байтах
 * out[20]  — результат (20 байт, big-endian)
 */
void sha1_compute(const uint8_t* data, size_t len, uint8_t out[20]);

#ifdef __cplusplus
}
#endif

#endif /* WS_CLIENT_SHA1_H */

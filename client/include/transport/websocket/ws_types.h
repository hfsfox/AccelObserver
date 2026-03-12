#ifndef __WS_TYPES_H__
#define __WS_TYPES_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// explicit client descriptor
typedef struct ws_client_t ws_client_t;

// error codes
typedef enum {
    WS_OK              =  0,
    WS_ERR_NET         = -1,  /* Network / socket error */
    WS_ERR_HANDSHAKE   = -2,  /* server reject Upgrade or invalid accept-key */
    WS_ERR_FRAME       = -3,  /* create or parse frame error */
    WS_ERR_CLOSED      = -4,  /* connection closed by server */
    WS_ERR_ALLOC       = -5,  /* low memory */
    WS_ERR_PARAM       = -6   /* invalid configuration */
} ws_error_code_t;

typedef struct {
    const char* host;              /**< server host */
    uint16_t    port;              /**< port */
    const char* path;              /**< URI path (default- "/") */
    uint32_t    connect_timeout_ms;/**< connection timeout, ms (0 = 5000) */
    uint32_t    handshake_timeout_ms; /**< handshake timeout, ms (0 = 5000) */
} ws_config_t;

#ifdef __cplusplus
}
#endif

#endif

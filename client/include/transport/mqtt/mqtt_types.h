#ifndef __MQTT_TYPES_H__
#define __MQTT_TYPES_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// explicit client descriptor
typedef struct mqtt_client_t mqtt_client_t;

// Error codes (like in ws_error_code_t)

typedef enum {
    MQTT_OK               =  0,
    MQTT_ERR_PARAM        = -1,   /* invalid configuration params */
    MQTT_ERR_ALLOC        = -2,   /* low memory */
    MQTT_ERR_CONNECT      = -3,   /* TCP-connection not created */
    MQTT_ERR_CONNACK      = -4,   /* broker reject CONNECT (CONNACK rc != 0) */
    MQTT_ERR_TIMEOUT      = -5,   /* No CONNACK in connect_timeout_ms period */
    MQTT_ERR_AUTH         = -6,   /* Broker reject auth */
    MQTT_ERR_TLS          = -7,   /* TLS setup error */
    MQTT_ERR_PUBLISH      = -8,   /* message pub error */
    MQTT_ERR_DISCONNECTED = -9,   /* Connection lost in run time */
    MQTT_ERR_LIB          = -10   /* libmosquitto internal error */
} mqtt_error_code_t;


#ifdef __cplusplus
}
#endif

#endif

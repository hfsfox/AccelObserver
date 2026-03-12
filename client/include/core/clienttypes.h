#ifndef __CLIENTTYPES_H__
#define __CLIENTTYPES_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    /* Connection */
    const char* protocol;
    const char* host;
    uint16_t    port;
    #ifdef HAVE_WEBSOCKET
    const char* path;
    #endif
    #ifdef HAVE_MQTT
    const char* topic;
    int         qos;
    const char* client_id;
    const char* username;
    const char* password;
    int         keepalive_sec;
    int         clean_session;   /* 1 = clean (default), 0 = persistent */
    /* TLS */
    /* TODO: implement secure options
    int         tls_enabled;
    const char* tls_cafile;
    const char* tls_capath;
    const char* tls_certfile;
    const char* tls_keyfile;
    int         tls_insecure;
    */

    /* Last Will */
    const char* will_topic;
    const char* will_payload;
    int         will_qos;
    int         will_retain;
    #endif // HAVE_MQTT

    /* Timing */
    double      duration_sec;
    double      rate_hz;
    uint32_t    connect_timeout_ms;

    /* Sensor */
    double      noise_amplitude;
    double      gravity_z;

    /* Misc */
    int         retain;
    int         verbose_level;
} app_config_t;

// MQTT section



#endif

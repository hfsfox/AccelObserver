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
    const char* topic;
    int         qos;
    const char* client_id;
    const char* username;
    const char* password;
    int         keepalive_sec;
    int         clean_session;   /* 1 = clean (default), 0 = persistent */
    /* TLS */
    int         tls_enabled;
    const char* tls_cafile;
    const char* tls_capath;
    const char* tls_certfile;
    const char* tls_keyfile;
    int         tls_insecure;

    /* Last Will */
    const char* will_topic;
    const char* will_payload;
    int         will_qos;
    int         will_retain;

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

// Last Will and Testament — unnecessary block

typedef struct
{
    const char* topic;    /* NULL → Will not used */
    const char* payload;  /* will be NULL (empty payload) */
    int         qos;      /* 0 / 1 / 2 */
    bool        retain;   /* Store last LWT-message on broker */
} mqtt_will_config_t;

// TLS conf — unnecessary block

typedef struct
{
    bool        enabled;         /* false → TLS not used */
    const char* cafile;          /* Path to CA-cert (PEM). NULL = system CA */
    const char* capath;          /* Alt: dir with CA-certs */
    const char* certfile;        /* client cert (mutual TLS). NULL = no mTLS */
    const char* keyfile;         /* Private client key */
    const char* ciphers;         /* OpenSSL cipher list. NULL = default */
    bool        insecure;        /* true → do not check hostname (only for debug!) */
    int         tls_version;     /* 0 = auto, 1 = TLSv1, 2 = TLSv1.1, 3 = TLSv1.2 */
} mqtt_tls_config_t;

//mqtt connection configuration
typedef struct
{
    /* Address and identification */
    const char*    host;                /* host/IP broker (required) */
    uint16_t       port;                /* port: 1883 (plain) / 8883 (TLS) */
    const char*    client_id;           /* MQTT Client ID. NULL if avto generated */
    bool           clean_session;       /* true = do not restore session */
    int            keepalive_sec;       /* PINGREQ. interval 0 - 60 s */

    /* Auth */
    const char*    username;            /* NULL - no auth */
    const char*    password;            /* may be NULL even with username */

    /* TLS */
    mqtt_tls_config_t  tls;

    /* Last Will */
    mqtt_will_config_t will;

    /* timeouts*/
    uint32_t       connect_timeout_ms;  	 /* CONNACK. wait 0 to 10 000 ms */
    uint32_t       reconnect_delay_min_sec;  /* Min. reconnect pause. 0 to 1 */
    uint32_t       reconnect_delay_max_sec;  /* Max. reconnect pause. 0 to 30 */
    bool           reconnect_exponential;    /* exponential backoff */
} mqtt_config_t;


#endif

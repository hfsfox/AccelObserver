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

typedef enum
{
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


typedef struct
{
    uint64_t published;    /* Число вызовов mqtt_publish (не подтверждения!) */
    uint64_t confirmed;    /* PUBACK/PUBREC получены (QoS ≥ 1) */
    uint64_t errors;       /* Ошибки при вызове mosquitto_publish */
    uint64_t reconnects;   /* Число автоматических переподключений */
} mqtt_stats_t;




#ifdef __cplusplus
}
#endif

#endif

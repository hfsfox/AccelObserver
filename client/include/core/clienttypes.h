#ifndef __CLIENTTYPES_H__
#define __CLIENTTYPES_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    /* Connection */
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
    bool        enabled;         /* false → TLS не используется */
    const char* cafile;          /* Путь к CA-сертификату (PEM). NULL = системный CA */
    const char* capath;          /* Альтернативно: директория с CA-сертификатами */
    const char* certfile;        /* Клиентский сертификат (mutual TLS). NULL = без mTLS */
    const char* keyfile;         /* Приватный ключ клиента */
    const char* ciphers;         /* OpenSSL cipher list. NULL = по умолчанию */
    bool        insecure;        /* true → не проверять hostname (только для отладки!) */
    int         tls_version;     /* 0 = auto, 1 = TLSv1, 2 = TLSv1.1, 3 = TLSv1.2 */
} mqtt_tls_config_t;

//mqtt connection configuration
typedef struct
{
    /* Адрес и идентификация */
    const char*    host;                /* Хост/IP брокера (обязательно) */
    uint16_t       port;                /* Порт: 1883 (plain) / 8883 (TLS) */
    const char*    client_id;           /* MQTT Client ID. NULL → автогенерация */
    bool           clean_session;       /* true = не восстанавливать сессию */
    int            keepalive_sec;       /* Интервал PINGREQ. 0 → 60 сек */

    /* Аутентификация */
    const char*    username;            /* NULL → без аутентификации */
    const char*    password;            /* Может быть NULL даже с username */

    /* TLS */
    mqtt_tls_config_t  tls;

    /* Last Will */
    mqtt_will_config_t will;

    /* Таймауты */
    uint32_t       connect_timeout_ms;  /* Ожидание CONNACK. 0 → 10 000 мс */
    uint32_t       reconnect_delay_min_sec;  /* Мин. пауза реконнекта. 0 → 1 */
    uint32_t       reconnect_delay_max_sec;  /* Макс. пауза реконнекта. 0 → 30 */
    bool           reconnect_exponential;    /* Экспоненциальный backoff */
} mqtt_config_t;

#endif

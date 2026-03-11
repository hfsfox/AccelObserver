#if !defined(_WIN32)
	#define _POSIX_C_SOURCE 200112L
	#define _DEFAULT_SOURCE  1
#endif

#include <transport/mqtt/mqtt_types.h>
#include <transport/mqtt/mqtt_client.h>
#include <network/net_platform.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>

#include <mosquitto.h>

struct mqtt_client_t
{
    struct mosquitto*         mosq;
    char*                     client_id_buf;
    atomic_bool               connected;
    atomic_int                connack_rc;
    atomic_bool               connack_received;
    atomic_bool               disconnecting;
    atomic_uint_least64_t     stat_published;
    atomic_uint_least64_t     stat_confirmed;
    atomic_uint_least64_t     stat_errors;
    atomic_uint_least64_t     stat_reconnects;
    int                       default_qos;
};

//
static const char* connack_str(int rc)
{
    switch (rc)
    {
        case 0: return "Connection Accepted";
        case 1: return "Refused: unacceptable protocol version";
        case 2: return "Refused: identifier rejected";
        case 3: return "Refused: server unavailable";
        case 4: return "Refused: bad user name or password";
        case 5: return "Refused: not authorized";
        default: return "Refused: unknown reason";
    }
}

//
static void
on_connect(struct mosquitto* mosq, void* userdata, int rc)
{
    (void)mosq;
    struct mqtt_client_t* c = (struct mqtt_client_t*)userdata;
    atomic_store(&c->connack_rc,       rc);
    atomic_store(&c->connack_received, true);
    if (rc == 0)
    {
        atomic_store(&c->connected, true);
        fprintf(stdout, "[mqtt] Connected to broker (CONNACK OK)\n");
    }
    else
    {
        atomic_store(&c->connected, false);
        fprintf(stderr, "[mqtt] CONNACK error: %s\n", connack_str(rc));
    }
}

//
static void
on_disconnect(struct mosquitto* mosq, void* userdata, int rc)
{
    (void)mosq;
    struct mqtt_client_t* c = (struct mqtt_client_t*)userdata;
    atomic_store(&c->connected, false);
    if (rc != 0 && !atomic_load(&c->disconnecting))
    {
        fprintf(stderr, "[mqtt] Unexpected disconnect (rc=%d), reconnecting...\n", rc);
        atomic_fetch_add(&c->stat_reconnects, 1);
        atomic_store(&c->connack_received, false);
    }
}

//
static void on_publish(struct mosquitto* mosq, void* userdata, int mid)
{
    (void)mosq; (void)mid;
    struct mqtt_client_t* c = (struct mqtt_client_t*)userdata;
    atomic_fetch_add(&c->stat_confirmed, 1);
}

//
static void on_message(struct mosquitto* mosq, void* userdata,
                        const struct mosquitto_message* msg)
{
    (void)mosq; (void)userdata; (void)msg;
}

//
static void
on_log(struct mosquitto* mosq, void* userdata,
                    int level, const char* str)
{
    (void)mosq; (void)userdata;
    if (level == MOSQ_LOG_ERR)
    {
        fprintf(stderr, "[mqtt/lib ERR] %s\n", str);
    }
#ifdef MQTT_DEBUG_LOG
    else 
    {
        fprintf(stdout, "[mqtt/lib DBG] %s\n", str);
    }
#endif
}

//
void mqtt_lib_init(void)    { mosquitto_lib_init(); }
void mqtt_lib_cleanup(void) { mosquitto_lib_cleanup(); }

//
static void generate_client_id(char* buf, size_t size)
{
	#ifdef _WIN32
    	unsigned long pid = (unsigned long)GetCurrentProcessId();
	#else
    	unsigned long pid = (unsigned long)getpid();
	#endif
    	snprintf(buf, size, "mqtt-client-%lu-%llu",
             pid, (unsigned long long)net_time_ms());
}

//
mqtt_client_t* mqtt_connect(const mqtt_config_t* cfg, mqtt_error_code_t* err_out)
{
    mqtt_error_code_t  fail_code = MQTT_OK;

    if (!cfg || !cfg->host || cfg->port == 0) {
        if (err_out) *err_out = MQTT_ERR_PARAM;
        return NULL;
    }

    struct mqtt_client_t* c = (struct mqtt_client_t*)calloc(1, sizeof(*c));
    if (!c) {
        if (err_out) *err_out = MQTT_ERR_ALLOC;
        return NULL;
    }

    atomic_init(&c->connected,        false);
    atomic_init(&c->connack_rc,       0);
    atomic_init(&c->connack_received, false);
    atomic_init(&c->disconnecting,    false);
    atomic_init(&c->stat_published,   0);
    atomic_init(&c->stat_confirmed,   0);
    atomic_init(&c->stat_errors,      0);
    atomic_init(&c->stat_reconnects,  0);
    
    // Client ID
    char auto_id[64];
    const char* cid = cfg->client_id;
    if (!cid || cid[0] == '\0')
    {
        generate_client_id(auto_id, sizeof(auto_id));
        cid = auto_id;
    }

    c->mosq = mosquitto_new(cid, cfg->clean_session, c);
    if (!c->mosq) {
        fprintf(stderr, "[mqtt] mosquitto_new failed (out of memory)\n");
        fail_code = MQTT_ERR_ALLOC;
        goto fail_free;
    }

    mosquitto_connect_callback_set   (c->mosq, on_connect);
    mosquitto_disconnect_callback_set(c->mosq, on_disconnect);
    mosquitto_publish_callback_set   (c->mosq, on_publish);
    mosquitto_message_callback_set   (c->mosq, on_message);
    mosquitto_log_callback_set       (c->mosq, on_log);
    
    //Authentication
    if (cfg->username && cfg->username[0] != '\0')
    {
        int rc = mosquitto_username_pw_set(c->mosq, cfg->username, cfg->password);
        if (rc != MOSQ_ERR_SUCCESS)
        {
            fprintf(stderr, "[mqtt] username_pw_set: %s\n", mosquitto_strerror(rc));
            fail_code = MQTT_ERR_AUTH;
            goto fail_destroy;
        }
    }
    //TLS
    if (cfg->tls.enabled)
    {
        int rc = mosquitto_tls_set(c->mosq,
                                    cfg->tls.cafile,
                                    cfg->tls.capath,
                                    cfg->tls.certfile,
                                    cfg->tls.keyfile,
                                    NULL);
        if (rc != MOSQ_ERR_SUCCESS)
        {
            fprintf(stderr, "[mqtt] tls_set: %s\n", mosquitto_strerror(rc));
            fail_code = MQTT_ERR_TLS;
            goto fail_destroy;
        }

        const char* ver_str = NULL;
        if      (cfg->tls.tls_version == 1) ver_str = "tlsv1";
        else if (cfg->tls.tls_version == 2) ver_str = "tlsv1.1";
        else if (cfg->tls.tls_version == 3) ver_str = "tlsv1.2";

        rc = mosquitto_tls_opts_set(c->mosq,
                                     cfg->tls.insecure ? 0 : 1,
                                     ver_str,
                                     cfg->tls.ciphers);
        if (rc != MOSQ_ERR_SUCCESS)
        {
            fprintf(stderr, "[mqtt] tls_opts_set: %s\n", mosquitto_strerror(rc));
            fail_code = MQTT_ERR_TLS;
            goto fail_destroy;
        }

        if (cfg->tls.insecure)
        {
            rc = mosquitto_tls_insecure_set(c->mosq, true);
            if (rc != MOSQ_ERR_SUCCESS)
            {
                fprintf(stderr, "[mqtt] tls_insecure_set: %s\n",
                        mosquitto_strerror(rc));
            }
        }
    }
    
    //last will
    
	if (cfg->will.topic && cfg->will.topic[0] != '\0')
	{
        const char* wpayload = cfg->will.payload ? cfg->will.payload : "";
        int wplen = (int)strlen(wpayload);
        int rc = mosquitto_will_set(c->mosq,
                                     cfg->will.topic,
                                     wplen, wpayload,
                                     cfg->will.qos,
                                     cfg->will.retain);
        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "[mqtt] will_set: %s (continuing)\n",
                    mosquitto_strerror(rc));
        }
    }
    
    // reconnect params
    {
        unsigned int dmin = cfg->reconnect_delay_min_sec > 0
                            ? (unsigned int)cfg->reconnect_delay_min_sec : 1u;
        unsigned int dmax = cfg->reconnect_delay_max_sec > 0
                            ? (unsigned int)cfg->reconnect_delay_max_sec : 30u;
        mosquitto_reconnect_delay_set(c->mosq, dmin, dmax,
                                      cfg->reconnect_exponential);
    }
    
    // TCP connect (synchronous)
    {
        int keepalive = (cfg->keepalive_sec > 0) ? cfg->keepalive_sec : 60;

        fprintf(stdout, "[mqtt] Connecting to %s:%u (client_id=%s keepalive=%ds)...\n",
                cfg->host, (unsigned)cfg->port, cid, keepalive);

        int rc = mosquitto_connect(c->mosq,
                                    cfg->host,
                                    (int)cfg->port,
                                    keepalive);
        if (rc != MOSQ_ERR_SUCCESS)
        {
            fprintf(stderr, "[mqtt] connect to %s:%u failed: %s\n",
                    cfg->host, (unsigned)cfg->port, mosquitto_strerror(rc));
            fail_code = MQTT_ERR_CONNECT;
            goto fail_destroy;
        }
    }
    
    // Backgroud I/O thread
    {
        int rc = mosquitto_loop_start(c->mosq);
        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "[mqtt] loop_start: %s\n", mosquitto_strerror(rc));
            fail_code = MQTT_ERR_LIB;
            goto fail_destroy;
        }
    }
    
    //Wait for CONNACK
    
    {
        uint32_t timeout_ms = (cfg->connect_timeout_ms > 0)
                              ? cfg->connect_timeout_ms : 10000u;
        uint64_t deadline   = net_time_ms() + timeout_ms;

        while (!atomic_load(&c->connack_received)) {
            if (net_time_ms() >= deadline) {
                fprintf(stderr, "[mqtt] Timeout waiting for CONNACK (%u ms)\n",
                        timeout_ms);
                mosquitto_loop_stop(c->mosq, true);
                fail_code = MQTT_ERR_TIMEOUT;
                goto fail_destroy;
            }
            net_sleep_ms(20);
        }

        int connack = atomic_load(&c->connack_rc);
        if (connack != 0) {
            fprintf(stderr, "[mqtt] CONNACK refused: %s\n", connack_str(connack));
            mosquitto_loop_stop(c->mosq, true);
            fail_code = (connack == 4 || connack == 5)
                        ? MQTT_ERR_AUTH : MQTT_ERR_CONNACK;
            goto fail_destroy;
        }
    }
    
    if (err_out) *err_out = MQTT_OK;
    return c;

fail_destroy:
    mosquitto_destroy(c->mosq);
    c->mosq = NULL;
fail_free:
    free(c);
    if (err_out) *err_out = fail_code;
    return NULL;
}

//
mqtt_error_code_t
mqtt_publish(mqtt_client_t* client,
                        const char* topic,
                        const char* payload,
                        size_t      len,
                        int         qos,
                        bool        retain)
{
    if (!client || !topic || !payload) return MQTT_ERR_PARAM;
    if (qos < 0 || qos > 2)           return MQTT_ERR_PARAM;

    if (!atomic_load(&client->connected))
        return MQTT_ERR_DISCONNECTED;

    int payloadlen = (len > 0) ? (int)len : (int)strlen(payload);
    int mid = 0;
    int rc  = mosquitto_publish(client->mosq,
                                 &mid,
                                 topic,
                                 payloadlen,
                                 payload,
                                 qos,
                                 retain);
    if (rc == MOSQ_ERR_SUCCESS) {
        atomic_fetch_add(&client->stat_published, 1);
        return MQTT_OK;
    }

    atomic_fetch_add(&client->stat_errors, 1);

    if (rc == MOSQ_ERR_NO_CONN || rc == MOSQ_ERR_CONN_LOST)
        return MQTT_ERR_DISCONNECTED;

    fprintf(stderr, "[mqtt] publish failed (rc=%d): %s\n",
            rc, mosquitto_strerror(rc));
    return MQTT_ERR_PUBLISH;
}

//
void
mqtt_disconnect(mqtt_client_t* client)
{
    if (!client) return;
    atomic_store(&client->disconnecting, true);
    if (client->mosq) {
        mosquitto_disconnect(client->mosq);
        mosquitto_loop_stop(client->mosq, false);
        mosquitto_destroy(client->mosq);
        client->mosq = NULL;
    }
    free(client->client_id_buf);
    free(client);
}

//
void
mqtt_destroy(mqtt_client_t* client)
{
    if (!client) return;
    atomic_store(&client->disconnecting, true);
    if (client->mosq) {
        mosquitto_loop_stop(client->mosq, true);
        mosquitto_destroy(client->mosq);
        client->mosq = NULL;
    }
    free(client->client_id_buf);
    free(client);
}

//
bool
mqtt_is_connected(const mqtt_client_t* client)
{
    return client && atomic_load(&client->connected);
}

void
mqtt_get_stats(const mqtt_client_t* client, mqtt_stats_t* out)
{
    if (!client || !out) return;
    out->published  = (uint64_t)atomic_load(&client->stat_published);
    out->confirmed  = (uint64_t)atomic_load(&client->stat_confirmed);
    out->errors     = (uint64_t)atomic_load(&client->stat_errors);
    out->reconnects = (uint64_t)atomic_load(&client->stat_reconnects);
}

const char*
mqtt_error_str(mqtt_error_code_t err)
{
    switch (err) {
        case MQTT_OK:               return "OK";
        case MQTT_ERR_PARAM:        return "Invalid parameters";
        case MQTT_ERR_ALLOC:        return "Memory allocation failed";
        case MQTT_ERR_CONNECT:      return "TCP connection failed";
        case MQTT_ERR_CONNACK:      return "Broker rejected CONNECT";
        case MQTT_ERR_TIMEOUT:      return "Timeout waiting for CONNACK";
        case MQTT_ERR_AUTH:         return "Authentication failed";
        case MQTT_ERR_TLS:          return "TLS configuration error";
        case MQTT_ERR_PUBLISH:      return "Publish failed";
        case MQTT_ERR_DISCONNECTED: return "Not connected";
        case MQTT_ERR_LIB:          return "libmosquitto internal error";
        default:                    return "Unknown error";
    }
}




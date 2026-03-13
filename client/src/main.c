#if !defined(_WIN32)
    #define _POSIX_C_SOURCE 200112L
    #define _DEFAULT_SOURCE  1
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>

#include <core/clienttypes.h>
#include <network/net_platform.h>
#include <misc/help.h>
#include <misc/conf_print.h>
#include <misc/conf_validator.h>
#include <misc/conf_argparse.h>

#include <sensor/sensor_data_sim.h>

#ifdef HAVE_MQTT
    #include <transport/mqtt/mqtt_types.h>
    #include <transport/mqtt/mqtt_client.h>
#endif

#ifdef HAVE_WEBSOCKET
    #include <transport/websocket/ws_types.h>
    #include <transport/websocket/ws_client.h>
#endif

#ifdef ENABLE_CONFPARSER
#include <confparser.h>
#endif

// global atomic values TODO: rewrite to platform atomic impl
static volatile sig_atomic_t g_running = 1;
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static const char*
extract_config_path(int argc, char* argv[])
{
    fprintf(stdout, "[INFO] finding config...\n");
    for (int i = 1; i < argc - 1; ++i)
        if (strcmp(argv[i], "--config") == 0)
        {
            return argv[i + 1];
        }
        return NULL;
}

int main(int argc, char* argv[])
{
    //const char* explicit_path = extract_config_path(argc, argv);

    app_config_t cfg = {0};

    // add parameters from conf file before parse_args cause user CLI input
    //has override priority
    const char* explicit_path = extract_config_path(argc, argv);
    char found_path[512] = {0};
    bool found = conf_find_config("data_client", explicit_path, found_path, sizeof(found_path));

    if (explicit_path == NULL && !found)
    {
        fprintf(stderr, "[FATAL] Config file not found: %s\n", explicit_path);
        return 1;
    }
    if (found && explicit_path != NULL)
    {
        fprintf(stdout, "[INFO] Config file loaded from: %s\n", explicit_path);
    }


    // now parse CLI arguments from user input according to priority

    cfg = parse_args(argc, argv);

    if (!validate_config(&cfg))
        return 1;

    // stop signal handlers
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // platform networking init
    if (!net_init()) {
        fprintf(stderr, "[FATAL] net_init: %s\n", net_last_error());
        return 1;
    }

    // data source init
    SensorConfig scfg = {
        .noise_amplitude = cfg.noise_amplitude,
        .gravity_z       = cfg.gravity_z,
        .device_path     = NULL
    };
    SensorCtx* sensor = sensor_init(&scfg);
    if (!sensor) {
        fprintf(stderr, "[FATAL] sensor_init failed\n");
        #if defined HAVE_MQTT
            mqtt_lib_cleanup();
        #endif
        net_cleanup();
        return 1;
    }

    #if defined HAVE_MQTT
    // libmosquitto init
    if(strcmp(cfg.protocol, "mqtt") == 0)
    {
    mqtt_lib_init();

    /* ---- mqtt_config_t ---- */
    mqtt_will_config_t will_cfg = {
        .topic   = cfg.will_topic,
        .payload = cfg.will_payload,
        .qos     = cfg.will_qos,
        .retain  = (bool)cfg.will_retain
    };

    /*
    mqtt_tls_config_t tls_cfg = {
        .enabled      = (bool)cfg.tls_enabled,
        .cafile       = cfg.tls_cafile,
        .capath       = cfg.tls_capath,
        .certfile     = cfg.tls_certfile,
        .keyfile      = cfg.tls_keyfile,
        .ciphers      = NULL,
        .insecure     = (bool)cfg.tls_insecure,
        .tls_version  = 0   // auto
    };*/

    mqtt_config_t mcfg = {
        .host                       = cfg.host,
        .port                       = cfg.port,
        .client_id                  = cfg.client_id,
        .clean_session              = (bool)cfg.clean_session,
        .keepalive_sec              = cfg.keepalive_sec,
        .username                   = cfg.username,
        .password                   = cfg.password,
        .tls                        = NULL, //tls_cfg,
        .will                       = will_cfg,
        .connect_timeout_ms         = cfg.connect_timeout_ms,
        .reconnect_delay_min_sec    = 1,
        .reconnect_delay_max_sec    = 30,
        .reconnect_exponential      = true
    };

    print_config(&cfg);
    printf("\n[mqtt_client] Connecting...\n");

    mqtt_error_code_t err;
    mqtt_client_t* client = mqtt_connect(&mcfg, &err);
    if (!client) {
        fprintf(stderr, "[FATAL] mqtt_connect: %s\n", mqtt_error_str(err));
        sensor_destroy(sensor);
        mqtt_lib_cleanup();
        net_cleanup();
        return 1;
    }

    long     total_expected = (long)(cfg.duration_sec * cfg.rate_hz);
    double   interval_ms    = 1000.0 / cfg.rate_hz;
    long     sent           = 0;
    long     errors         = 0;
    uint64_t start_ms       = net_time_ms();
    char     json_buf[256];

    printf("[mqtt_client] Connected | %ld packets @ %.1f Hz for %.1f s\n\n",
           total_expected, cfg.rate_hz, cfg.duration_sec);

    while (g_running) {
        uint64_t elapsed_ms = net_time_ms() - start_ms;
        if ((double)elapsed_ms >= cfg.duration_sec * 1000.0)
            break;

        if (!mqtt_is_connected(client)) {
            fprintf(stderr, "[WARN] Broker disconnected, waiting for reconnect...\n");
            net_sleep_ms(500);
            /* Ждём реконнект максимум 10 секунд */
            uint64_t wait_start = net_time_ms();
            while (!mqtt_is_connected(client) && g_running) {
                if (net_time_ms() - wait_start > 10000) {
                    fprintf(stderr, "[ERROR] Reconnect timeout, aborting\n");
                    goto done;
                }
                net_sleep_ms(200);
            }
        }

        SensorPacket pkt;
        if (!sensor_read(sensor, &pkt)) {
            fprintf(stderr, "[WARN] sensor_read failed (seq=%ld)\n", sent);
            ++errors;
            net_sleep_ms((uint32_t)interval_ms);
            continue;
        }

        int jlen = sensor_to_json(&pkt, json_buf, sizeof(json_buf));
        if (jlen <= 0) {
            fprintf(stderr, "[WARN] sensor_to_json failed\n");
            ++errors;
            continue;
        }

        mqtt_error_code_t pub_err = mqtt_publish(client,
                                         cfg.topic,
                                         json_buf,
                                         (size_t)jlen,
                                         cfg.qos,
                                         (bool)cfg.retain);
        if (pub_err != MQTT_OK) {
            fprintf(stderr, "[ERROR] mqtt_publish: %s\n",
                    mqtt_error_str(pub_err));
            ++errors;
            if (pub_err == MQTT_ERR_DISCONNECTED) {
                net_sleep_ms((uint32_t)interval_ms);
                continue;
            }
            break;
        }

        ++sent;

        if (cfg.verbose_level) {
            printf("[mqtt_client] sent #%ld: %s\n", sent, json_buf);
        } else if (sent % (long)(total_expected / 10 + 1) == 0) {
            double pct = 100.0 * (double)(net_time_ms() - start_ms)
            / (cfg.duration_sec * 1000.0);
            printf("[mqtt_client] %5.1f%% | seq=%-6ld | %s\n",
                   pct, sent - 1, json_buf);
        }

        uint64_t next_tick_ms = start_ms + (uint64_t)((double)sent * interval_ms);
        uint64_t now_ms       = net_time_ms();
        if (next_tick_ms > now_ms)
            net_sleep_ms((uint32_t)(next_tick_ms - now_ms));
    }

    done:;
    double elapsed_total = (double)(net_time_ms() - start_ms) / 1000.0;
    double actual_hz     = (elapsed_total > 0.0)
    ? (double)sent / elapsed_total : 0.0;

    mqtt_stats_t stats;
    mqtt_get_stats(client, &stats);

    printf("\n[mqtt_client] Disconnecting...\n");
    mqtt_disconnect(client);

    sensor_destroy(sensor);
    mqtt_lib_cleanup();
    net_cleanup();

    printf("[mqtt_client] Done: %ld sent in %.2f s (%.1f Hz) | errors: %ld\n",
           sent, elapsed_total, actual_hz, errors);
    printf("  published  : %llu (mosquitto_publish calls)\n",
           (unsigned long long)stats.published);
    printf("  confirmed  : %llu (PUBACK/PUBREC received, QoS>=1)\n",
           (unsigned long long)stats.confirmed);
    printf("  reconnects : %llu\n",
           (unsigned long long)stats.reconnects);

    return (errors > 0) ? 2 : 0;
    }
    #endif
    #if defined HAVE_WEBSOCKET
    if(strcmp(cfg.protocol, "ws") == 0)
    {
        ws_config_t wcfg = {
            .host                 = cfg.host,
            .port                 = cfg.port,
            .path                 = cfg.path,
            .connect_timeout_ms   = 5000,
            .handshake_timeout_ms = 5000
        };

        print_config(&cfg);

        printf("[ws_client] Connecting to ws://%s:%u%s ...\n",
               cfg.host, cfg.port, cfg.path);

        ws_error_code_t err;
        ws_client_t* ws = ws_connect(&wcfg, &err);
        if (!ws) {
            fprintf(stderr, "[FATAL] ws_connect: %s | %s\n",
                    ws_error_str(err), net_last_error());
            sensor_destroy(sensor);
            net_cleanup();
            return 1;
        }

        long total_expected = (long)(cfg.duration_sec * cfg.rate_hz);
        double interval_ms  = 1000.0 / cfg.rate_hz;

        printf("[ws_client] Connected | %ld packets @ %.1f Hz for %.1f s\n",
               total_expected, cfg.rate_hz, cfg.duration_sec);

        long   sent       = 0;
        long   errors     = 0;
        uint64_t start_ms = net_time_ms();
        char   json_buf[256];

        while (g_running) {
            uint64_t elapsed_ms = net_time_ms() - start_ms;
            if ((double)elapsed_ms >= cfg.duration_sec * 1000.0) break;

            /* Читаем пакет из датчика */
            SensorPacket pkt;
            if (!sensor_read(sensor, &pkt)) {
                fprintf(stderr, "[WARN] sensor_read failed (seq=%ld)\n", sent);
                ++errors;
                net_sleep_ms((uint32_t)interval_ms);
                continue;
            }

            /* Сериализация в JSON */
            int jlen = sensor_to_json(&pkt, json_buf, sizeof(json_buf));
            if (jlen <= 0) {
                fprintf(stderr, "[WARN] sensor_to_json failed\n");
                ++errors;
                continue;
            }

            ws_error_code_t send_err = ws_send_text(ws, json_buf, (size_t)jlen);
            if (send_err != WS_OK) {
                fprintf(stderr, "[ERROR] ws_send_text: %s\n", ws_error_str(send_err));
                ++errors;
                if (send_err == WS_ERR_NET || send_err == WS_ERR_CLOSED) break;
                continue;
            }

            ++sent;

            if (cfg.verbose_level) {
                printf("[ws_client] sent #%ld: %s\n", sent, json_buf);
            } else if (sent % (long)(total_expected / 10 + 1) == 0) {
                double pct = 100.0 * (double)(net_time_ms() - start_ms)
                / (cfg.duration_sec * 1000.0);
                printf("[ws_client] %5.1f%% | seq=%-6ld | %s\n",
                       pct, sent - 1, json_buf);
            }

            uint64_t next_tick_ms = start_ms + (uint64_t)((double)sent * interval_ms);
            uint64_t now_ms       = net_time_ms();
            if (next_tick_ms > now_ms) {
                net_sleep_ms((uint32_t)(next_tick_ms - now_ms));
            }
        }

        double elapsed_total = (double)(net_time_ms() - start_ms) / 1000.0;
        double actual_hz     = (elapsed_total > 0.0) ? (double)sent / elapsed_total : 0.0;

        printf("\n[ws_client] Closing connection...\n");
        ws_close(ws);

        sensor_destroy(sensor);
        net_cleanup();

        printf("[ws_client] Done: %ld packets in %.2f s (%.1f Hz) | errors: %ld\n",
               sent, elapsed_total, actual_hz, errors);

        return (errors > 0) ? 2 : 0;
    }
    #endif
}

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
#include <transport/mqtt/mqtt_types.h>

// global atomic values TODO: rewrite to platform atomic impl
static volatile sig_atomic_t g_running = 1;
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

int
main(int argc, char* argv[])
{
    app_config_t cfg = parse_args(argc, argv);

    if (!validate_config(&cfg))
    {
        fprintf(stderr,"[ERROR] invalid configuration\n");
        return 1;
    }

    // add stop signal handlers
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // platform networking init
    if (!net_init())
    {
        fprintf(stderr, "[FATAL] net_init: %s\n", net_last_error());
        net_cleanup();
        return 1;
    }


    mqtt_will_config_t will_cfg =
    {
        .topic   = cfg.will_topic,
        .payload = cfg.will_payload,
        .qos     = cfg.will_qos,
        .retain  = (bool)cfg.will_retain
    };

    mqtt_tls_config_t tls_cfg =
    {
        .enabled      = (bool)cfg.tls_enabled,
        .cafile       = cfg.tls_cafile,
        .capath       = cfg.tls_capath,
        .certfile     = cfg.tls_certfile,
        .keyfile      = cfg.tls_keyfile,
        .ciphers      = NULL,
        .insecure     = (bool)cfg.tls_insecure,
        .tls_version  = 0   /* auto */
    };

    mqtt_config_t mcfg =
    {
        .host                       = cfg.host,
        .port                       = cfg.port,
        .client_id                  = cfg.client_id,
        .clean_session              = (bool)cfg.clean_session,
        .keepalive_sec              = cfg.keepalive_sec,
        .username                   = cfg.username,
        .password                   = cfg.password,
        .tls                        = tls_cfg,
        .will                       = will_cfg,
        .connect_timeout_ms         = cfg.connect_timeout_ms,
        .reconnect_delay_min_sec    = 1,
        .reconnect_delay_max_sec    = 30,
        .reconnect_exponential      = true
    };

    print_config(&cfg);
    printf("\n[mqtt_client] Connecting...\n");
    
    mqtt_error_code_t err;
    /*
    mqtt_client_t* client = mqtt_connect(&mcfg, &err);
    if (!client) {
        fprintf(stderr, "[FATAL] mqtt_connect: %s\n", mqtt_error_str(err));
        sensor_destroy(sensor);
        mqtt_lib_cleanup();
        net_cleanup();
        return 1;
    }*/

    return 0;
}

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
//#include "platform/net_platform.h"

// global atomic values TODO: rewrite to platform atomic impl
static volatile sig_atomic_t g_running = 1;
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

// parse cli arguments
static app_config_t
parse_args(int argc, char* argv[])
{
    // default configuration fill
    app_config_t cfg =
    {
    	.protocol           = "mqtt",
        .host               = "localhost",
        .port               = 1883,
        .topic              = "sensors/accel",
        .qos                = 0,
        .client_id          = NULL,
        .username           = NULL,
        .password           = NULL,
        .keepalive_sec      = 60,
        .clean_session      = 1,

        .tls_enabled        = 0,
        .tls_cafile         = NULL,
        .tls_capath         = NULL,
        .tls_certfile       = NULL,
        .tls_keyfile        = NULL,
        .tls_insecure       = 0,

        .will_topic         = NULL,
        .will_payload       = NULL,
        .will_qos           = 0,
        .will_retain        = 0,

        .duration_sec       = 10.0,
        .rate_hz            = 10.0,
        .connect_timeout_ms = 10000,

        .noise_amplitude    = 0.05,
        .gravity_z          = 9.81,

        .retain             = 0,
        .verbose_level      = 0
    };
    // fill app config vith user values by cli args parse
    for (int i = 1; i < argc; ++i)
    {
        if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h")    == 0)
        {
            print_usage(argv[0]); exit(0);
        }
        //connection parameters
        /*else if(strcmp(argv[i], "--protocol") == 0 && i + 1 < argc)
        {
        	if()
        	{
        	}
        	else if()
        	else
        	{
        	}
        }*/
        else if(strcmp(argv[i], "--host") == 0 && i + 1 < argc)
        {
            cfg.host = argv[++i];
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            cfg.port = (uint16_t)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--topic") == 0 && i + 1 < argc)
        {
            cfg.topic = argv[++i];
        }
        else if (strcmp(argv[i], "--qos") == 0 && i + 1 < argc)
        {
            cfg.qos = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--id") == 0 && i + 1 < argc)
        {
            cfg.client_id = argv[++i];
        }
        else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc)
        {
            cfg.username = argv[++i];
        }
        else if (strcmp(argv[i], "--pass") == 0 && i + 1 < argc)
        {
            cfg.password = argv[++i];
        }
        else if (strcmp(argv[i], "--keepalive") == 0 && i + 1 < argc)
        {
            cfg.keepalive_sec = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--no-clean") == 0)
        {
            cfg.clean_session = 0;
        }
        // TLS configuration
        else if (strcmp(argv[i], "--tls") == 0)
        {
            cfg.tls_enabled = 1;
            // shift port to 8883 only if port not defined explicitly
            if (cfg.port == 1883) cfg.port = 8883;

            if (strcmp(argv[i], "--cafile") == 0 && i + 1 < argc)
            {
                cfg.tls_cafile    = argv[++i];
                cfg.tls_enabled   = 1;
            }
            else if (strcmp(argv[i], "--capath") == 0 && i + 1 < argc)
            {
                cfg.tls_capath    = argv[++i];
                cfg.tls_enabled   = 1;
            }
            else if (strcmp(argv[i], "--cert") == 0 && i + 1 < argc)
            {
                cfg.tls_certfile  = argv[++i];
            }
            else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc)
            {
                cfg.tls_keyfile   = argv[++i];
            }
            else if (strcmp(argv[i], "--tls-insecure") == 0)
            {
                cfg.tls_insecure  = 1;
            }
        }
        // last will
        else if (strcmp(argv[i], "--will-topic") == 0 && i + 1 < argc)
        {
            cfg.will_topic   = argv[++i];
        }
        else if (strcmp(argv[i], "--will-payload") == 0 && i + 1 < argc)
        {
            cfg.will_payload = argv[++i];
        }
        else if (strcmp(argv[i], "--will-qos") == 0 && i + 1 < argc)
        {
            cfg.will_qos     = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--will-retain") == 0)
        {
            cfg.will_retain  = 1;
        }
        // timing
        else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
        {
            cfg.duration_sec = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc)
        {
            cfg.rate_hz = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc)
        {
            cfg.connect_timeout_ms = (uint32_t)atoi(argv[++i]);
        }
        // fake sensor settings
        else if (strcmp(argv[i], "--noise") == 0 && i + 1 < argc)
        {
            cfg.noise_amplitude = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "--gravity") == 0 && i + 1 < argc)
        {
            cfg.gravity_z = atof(argv[++i]);
        }
        // misc.
        else if (strcmp(argv[i], "--retain") == 0)
        {
            cfg.retain  = 1;
        }
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0)
        {
            cfg.verbose_level = 1;
        }
        else
        {
            fprintf(stderr, "[WARN] Unknown argument: %s\n", argv[i]);
        }
    }

    return cfg;
}

// config struct validation
static int validate_config(const app_config_t* cfg) {
    int ok = 1;

	if (cfg->protocol == "mqtt")
	{
		ok =0;
	}
    if (cfg->port == 0)
    {
        fprintf(stderr, "[ERROR] --port must be > 0\n"); ok = 0;
    }
    if (cfg->qos < 0 || cfg->qos > 2)
    {
        fprintf(stderr, "[ERROR] --qos must be 0, 1 or 2\n"); ok = 0;
    }
    if (cfg->will_qos < 0 || cfg->will_qos > 2)
    {
        fprintf(stderr, "[ERROR] --will-qos must be 0, 1 or 2\n"); ok = 0;
    }
    if (cfg->rate_hz <= 0.0 || cfg->rate_hz > 10000.0)
    {
        fprintf(stderr, "[ERROR] --rate must be in (0, 10000] Hz\n"); ok = 0;
    }
    if (cfg->duration_sec <= 0.0)
    {
        fprintf(stderr, "[ERROR] --duration must be > 0\n"); ok = 0;
    }
    if (cfg->keepalive_sec < 0)
    {
        fprintf(stderr, "[ERROR] --keepalive must be >= 0\n"); ok = 0;
    }
    /* mTLS: cert и key идут парой */
    if ((cfg->tls_certfile != NULL) != (cfg->tls_keyfile != NULL))
    {
        fprintf(stderr, "[ERROR] --cert and --key must be used together\n"); ok = 0;
    }
    /* Предупреждение: --will-payload без --will-topic */
    if (cfg->will_payload && !cfg->will_topic)
    {
        fprintf(stderr, "[WARN] --will-payload has no effect without --will-topic\n");
    }
    /* QoS > 0 + clean session: предупредить (не ошибка) */
    if (cfg->qos > 0 && cfg->clean_session)
    {
        fprintf(stdout,
                "[WARN] QoS %d with clean session=true: "
                "queued messages will be lost on reconnect. "
                "Consider --no-clean\n", cfg->qos);
    }
    return ok;
}

static void
print_config(const app_config_t* cfg)
{
    printf("[mqtt_client] Configuration:\n");
    printf("  Broker    : %s://%s:%u\n",
           cfg->tls_enabled ? "mqtts" : "mqtt",
           cfg->host, (unsigned)cfg->port);
    printf("  Topic     : %s\n",  cfg->topic);
    printf("  QoS       : %d\n",  cfg->qos);
    printf("  Client ID : %s\n",  cfg->client_id ? cfg->client_id : "(auto)");
    printf("  Keepalive : %d s\n", cfg->keepalive_sec);
    printf("  Session   : %s\n",  cfg->clean_session ? "clean" : "persistent");
    if (cfg->username)
        printf("  Auth      : user=%s\n", cfg->username);
    if (cfg->tls_enabled)
    {
        printf("  TLS       : enabled\n");
        if (cfg->tls_cafile)   printf("  CA file   : %s\n", cfg->tls_cafile);
        if (cfg->tls_capath)   printf("  CA path   : %s\n", cfg->tls_capath);
        if (cfg->tls_certfile) printf("  Cert      : %s\n", cfg->tls_certfile);
        if (cfg->tls_insecure) printf("  [!] TLS hostname verification DISABLED\n");
    }
    if (cfg->will_topic)
        printf("  LWT       : topic=%s qos=%d retain=%d\n",
               cfg->will_topic, cfg->will_qos, cfg->will_retain);
        printf("  Duration  : %.1f s @ %.1f Hz (%ld packets)\n",
               cfg->duration_sec, cfg->rate_hz,
               (long)(cfg->duration_sec * cfg->rate_hz));
        printf("  Sensor    : noise=%.3f gravity=%.4f\n",
               cfg->noise_amplitude, cfg->gravity_z);
        printf("  Retain    : %s\n",  cfg->retain  ? "yes" : "no");
}

int main(int argc, char* argv[])
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
    
    

    return 0;
}

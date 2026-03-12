#ifndef __CONF_ARGPARSE_H__
#define __CONF_ARGPARSE_H__

#include <core/clienttypes.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// parse cli arguments
app_config_t
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

        /* TODO: implement secure options
        .tls_enabled        = 0,
        .tls_cafile         = NULL,
        .tls_capath         = NULL,
        .tls_certfile       = NULL,
        .tls_keyfile        = NULL,
        .tls_insecure       = 0,
        */

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
        else if(strcmp(argv[i], "--host") == 0 && i + 1 < argc)
        {
            cfg.host = argv[++i];
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            cfg.port = (uint16_t)atoi(argv[++i]);
        }
        else if(strcmp(argv[i], "--protocol") == 0 && i + 1 < argc)
        {
            cfg.protocol = argv[++i];
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
        /*
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
        */
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

#ifdef __cplusplus
}
#endif

#endif

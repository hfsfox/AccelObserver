#ifndef __CONF_PRINT_H__
#define __CONF_PRINT_H__

#include <core/clienttypes.h>
#include <stdio.h>

void
print_config(const app_config_t* cfg)
{
    if(cfg->protocol == "mqtt"){
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
    else if(cfg->protocol == "ws")
    {
        printf("[ws_client] Configuration:\n");
        printf("  Connect   :\n",
               cfg->host, (unsigned)cfg->port);
    }
}

#endif

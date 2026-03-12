#include <misc/conf_validator.h>

int validate_config(const app_config_t* cfg)
{
    int ok = 1;

	if (cfg->protocol != "mqtt" || cfg->protocol != "ws")
	{
		fprintf(stderr, "[ERROR] --protocol must be mqtt or ws\n"); ok =0;
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
    /* QoS > 0 + clean session: warn (no error) */
    if (cfg->qos > 0 && cfg->clean_session)
    {
        fprintf(stdout,
                "[WARN] QoS %d with clean session=true: "
                "queued messages will be lost on reconnect. "
                "Consider --no-clean\n", cfg->qos);
    }
    return ok;
}

 /*
  * Fake sensor implementation for test purposes
  * produce sinusoid moves + x and y axis noise
  * g accel - ~9.81 m/s^2 on z axis
  * sensorctx -- context for real sensor data, replace with DMA, buf or fd.
  * header -- stable interface
  */
 
#include <sensor/sensor_data_sim.h>
#include <network/net_platform.h>  /* net_time_ms() */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <time.h>

struct SensorCtx {
    SensorConfig config;
    uint32_t     sequence;    /* current sequense */
    double       phase;       /* current sin phase for movement imitation */
    uint32_t     rng_state;   /* PRNG state (xorshift32) */
};

/* default config */
const SensorConfig SENSOR_CONFIG_DEFAULT = {
    .noise_amplitude = 0.05,
    .gravity_z       = 9.81,
    .device_path     = NULL
};

// Xorshift32 PRNG — fast and enough for noise simulation
static uint32_t prng_next(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x <<  5u;
    *state = x;
    return x;
}

/* value [-1.0, 1.0] */
static double prng_f64(uint32_t* state) {
    uint32_t v = prng_next(state);
    /* normalize [0, UINT32_MAX] → [-1, 1] */
    return ((double)(v >> 1u) / (double)(UINT32_MAX >> 1u)) - 1.0;
}

/* --------------------------------------------------------------------------- */
SensorCtx* sensor_init(const SensorConfig* cfg) {
    SensorCtx* ctx = (SensorCtx*)malloc(sizeof(SensorCtx));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(*ctx));

    ctx->config   = cfg ? *cfg : SENSOR_CONFIG_DEFAULT;
    ctx->sequence = 0;
    ctx->phase    = 0.0;

    /* PRNG init with time + PID */
    uint32_t seed = (uint32_t)time(NULL);
#ifdef _WIN32
    seed ^= (uint32_t)GetCurrentProcessId();
#else
    seed ^= (uint32_t)getpid();
#endif
    ctx->rng_state = (seed == 0) ? 1u : seed;

    return ctx;
}

/* --------------------------------------------------------------------------- */
bool sensor_read(SensorCtx* ctx, SensorPacket* out) {
    if (!ctx || !out) return false;

    /* timestamp on client device */
    out->timestamp_ms = net_time_ms();
    out->sequence_id  = ctx->sequence++;

    /* Mock-данные:
     *   X = 0.3 * sin(phase)       — horizontal move simulation
     *   Y = 0.3 * cos(phase * 0.7) — with another f
     *   Z approximately gravity_z
     *  +/- noise_amplitude
     */
    double noise_x = prng_f64(&ctx->rng_state) * ctx->config.noise_amplitude;
    double noise_y = prng_f64(&ctx->rng_state) * ctx->config.noise_amplitude;
    double noise_z = prng_f64(&ctx->rng_state) * ctx->config.noise_amplitude;

    out->acc_x = 0.3 * sin(ctx->phase) + noise_x;
    out->acc_y = 0.3 * cos(ctx->phase * 0.7) + noise_y;
    out->acc_z = ctx->config.gravity_z + noise_z;

    /* increment of phase ~0.1 rad / tick with 10 Гц = ~0.57 r/s */
    ctx->phase += 0.1;
    if (ctx->phase > 6.283185307) ctx->phase -= 6.283185307; /* 2*pi */

    return true;
}

/* --------------------------------------------------------------------------- */
void sensor_destroy(SensorCtx* ctx) {
    if (ctx) free(ctx);
}

/* --------------------------------------------------------------------------- */
int sensor_to_json(const SensorPacket* pkt, char* buf, size_t buf_size) {
    if (!pkt || !buf || buf_size == 0) return -1;
    int n = snprintf(buf, buf_size,
        "{\"ts\":%llu,\"seq\":%u,\"ax\":%.6f,\"ay\":%.6f,\"az\":%.6f}",
        (unsigned long long)pkt->timestamp_ms,
        (unsigned)pkt->sequence_id,
        pkt->acc_x,
        pkt->acc_y,
        pkt->acc_z);
    if (n < 0 || (size_t)n >= buf_size) return -1;
    return n;
}

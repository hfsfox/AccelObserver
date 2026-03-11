#ifndef WS_CLIENT_SENSOR_DATA_H
#define WS_CLIENT_SENSOR_DATA_H
/*
 * sensor/sensor_data_sim.h
 * Accel data source
 *
 * Usage cycle:
 *   1. sensor_init()  — сcreate context (mem alloc)
 *   2. sensor_read()  — read packets in loop
 *   3. sensor_destroy() — free resources
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif



// data packet  synchronized with server types defines
typedef struct
{
    uint64_t timestamp_ms; /**< Unix timestamp in msecs (UTC) */
    uint32_t sequence_id;  /**< monotonic sequence id */
    double   acc_x;        /**< accel X, m/s^2 */
    double   acc_y;        /**< accel Y, m/s^2 */
    double   acc_z;        /**< accel Z, m/s^2 */
} SensorPacket;

typedef struct SensorCtx SensorCtx;

// init parameters (fake sensor use noise_amplitude only)
typedef struct
{
    double  noise_amplitude; // noise amplitude in m/s^2
    double  gravity_z;       // gravity accel on Z, m/s^2. default 9.81
    const char* device_path; // real devfile path (only for *nix) (not used)
} SensorConfig;

/* default config — can pass NULL to sensor_init() */
extern const
SensorConfig SENSOR_CONFIG_DEFAULT;


//API
SensorCtx*
sensor_init(const SensorConfig* cfg);

bool
sensor_read(SensorCtx* ctx, SensorPacket* out);

void
sensor_destroy(SensorCtx* ctx);
 
// serialize packet to JSON string compatible to server
// format:  {"ts":...,"seq":...,"ax":...,"ay":...,"az":...}
int
sensor_to_json(const SensorPacket* pkt, char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* WS_CLIENT_SENSOR_DATA_H */

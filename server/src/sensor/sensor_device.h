#ifndef SENSOR_DEVICE_H
#define SENSOR_DEVICE_H
/*
 * sensor/sensor_device.h
 * Arbitrary accelerometer device descriptor and emulation engine.
 *
 * Supports any IMU/accelerometer by parameterizing:
 *   - measurement range in g
 *   - ADC resolution (m/s^2 per LSB)
 *   - noise density (g/sqrt(Hz))
 *   - sampling rate limits
 *   - quantization and zero-offset errors
 *
 * Pre-defined profiles: BMI160, MPU6050, ADXL345, generic.
 * Custom profiles are loaded from the [device] section of the INI config.
 *
 * Android sensor source:
 *   When android_host/port are set, sensor_device_read() connects to
 *   the "Sensor Server" Android app (github.com/umer0586/SensorServer)
 *   which streams sensor data over WebSocket in JSON format:
 *     {"x":0.12,"y":-9.81,"z":0.03,"timestamp":123456789}
 *   The client relays these values instead of generating mock data.
 *
 * Recommended Android app for sensor simulation:
 *   "Sensor Server" by umer0586
 *   - Available on F-Droid and GitHub releases
 *   - Streams all Android sensors via WebSocket on local WiFi
 *   - URL pattern: ws://<phone-ip>:<port>/sensor/connect?type=android.sensor.accelerometer
 *   - No root required, works on Android 5+
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <confparser.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum string field length in DeviceDescriptor */
#define DEVICE_MODEL_MAX      32
#define DEVICE_MODE_MAX       16
#define DEVICE_ANDROID_MAX   128

/*
 * DeviceDescriptor — full specification of a physical or emulated sensor.
 * All acceleration values are in m/s^2 (SI units, compatible with Android).
 */
typedef struct {
    /* Identification */
    char     model[DEVICE_MODEL_MAX];       /* e.g. "BMI160", "MPU6050", "generic" */

    /* Measurement range */
    float    range_g;                       /* Full-scale range in g, e.g. 16.0 */
    float    range_ms2;                     /* Derived: range_g * 9.80665 */

    /* ADC quantization */
    float    resolution_ms2;                /* m/s^2 per LSB (quantum step size) */
    bool     apply_quantization;            /* Round output to resolution grid */

    /* Noise model */
    float    noise_density_g_sqhz;          /* Noise density in g/sqrt(Hz) */
    /* Noise floor at given bandwidth: sigma = noise_density * sqrt(bandwidth_hz) */

    /* Zero-g offset (bias) per axis in m/s^2 */
    float    zero_offset_x;
    float    zero_offset_y;
    float    zero_offset_z;

    /* Timing */
    uint32_t min_delay_us;                  /* Minimum sampling interval (us) */
    uint32_t max_delay_us;                  /* Maximum sampling interval (us) */

    /* Reporting behavior */
    char     reporting_mode[DEVICE_MODE_MAX]; /* "continuous", "on-change", "one-shot" */
    bool     wake_up;                        /* Can wake the system from sleep */

    /*
     * Android sensor source (optional).
     * When android_host is non-empty, sensor_device_read() connects to the
     * Sensor Server app running on the specified Android device.
     */
    char     android_host[DEVICE_ANDROID_MAX];
    uint16_t android_port;

    /* Runtime state (internal, zeroed by sensor_device_default) */
    float    _noise_sigma;                  /* Pre-computed: noise_density * sqrt(bw) */
    double   _rng_state;                    /* Xorshift64 PRNG state */
} DeviceDescriptor;

/* Built-in sensor profiles */
typedef enum {
    DEVICE_PROFILE_GENERIC  = 0,
    DEVICE_PROFILE_BMI160   = 1,
    DEVICE_PROFILE_MPU6050  = 2,
    DEVICE_PROFILE_ADXL345  = 3
} DeviceProfile;

/* --------------------------------------------------------------------------- */
/* Initialization */
/* --------------------------------------------------------------------------- */

/* Fill descriptor with generic default values. Must be called first. */
void sensor_device_default(DeviceDescriptor* dev);

/* Fill descriptor with a known sensor profile. */
void sensor_device_preset(DeviceDescriptor* dev, DeviceProfile profile);

/*
 * Finalize descriptor after all fields are set.
 * Derives range_ms2, _noise_sigma, seeds PRNG.
 * Must be called before sensor_device_read().
 */
void sensor_device_init(DeviceDescriptor* dev, double bandwidth_hz);

/*
 * Load [device] section from an already-parsed IniDoc into descriptor.
 * Requires config_parser.h to be included by the caller.
 * Returns true if at least one field was loaded.
 */
struct conf_result_t;
bool sensor_device_load_ini(DeviceDescriptor* dev, const struct conf_result_t* ini);

/* --------------------------------------------------------------------------- */
/* Sampling */
/* --------------------------------------------------------------------------- */

/*
 * Generate one accelerometer sample into (ax, ay, az).
 * Values are in m/s^2. Applies noise, quantization, and zero-offset.
 * gravity_z is the static gravity component on Z axis (m/s^2).
 *
 * If android_host is set, this function is a no-op placeholder;
 * use sensor_device_read_android() for remote sources.
 */
void sensor_device_read(const DeviceDescriptor* dev,
                         float gravity_z,
                         float* ax, float* ay, float* az,
                         uint32_t seq);

/* --------------------------------------------------------------------------- */
/* Diagnostics */
/* --------------------------------------------------------------------------- */

/* Print device descriptor summary to stdout. */
void sensor_device_print(const DeviceDescriptor* dev);

/*
 * Noise floor analysis:
 * Returns estimated RMS noise (m/s^2) at the given sampling rate.
 * noise_floor = noise_density_g * 9.80665 * sqrt(sample_rate_hz / 2)
 */
float sensor_device_noise_floor_ms2(const DeviceDescriptor* dev,
                                      float sample_rate_hz);

/*
 * Minimum detectable signal (MDS) in m/s^2 at given sample rate.
 * MDS = 3 * noise_floor (3-sigma threshold for reliable detection)
 */
float sensor_device_mds_ms2(const DeviceDescriptor* dev, float sample_rate_hz);

/*
 * Required sample rate (Hz) to capture vibrations up to max_freq_hz.
 * Applies Nyquist-Shannon theorem: fs >= 2 * max_freq_hz.
 * Returns recommended fs with 20% safety margin: fs = 2.4 * max_freq_hz.
 */
float sensor_device_nyquist_rate(float max_freq_hz);

/*
 * Recommended range_g for a given peak vibration amplitude (m/s^2).
 * Selects the smallest standard range that can accommodate the signal
 * with 20% headroom. Standard ranges: 2, 4, 8, 16 g.
 */
float sensor_device_recommend_range_g(float peak_ms2);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_DEVICE_H */

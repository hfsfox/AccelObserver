/*
 * sensor/sensor_device.h
 * Single-header accelerometer device descriptor and emulation engine.
 *
 * Usage pattern (stb-style):
 * ==========================
 *
 *   In EXACTLY ONE translation unit (.c file) define the implementation:
 *
 *       #define SENSOR_DEVICE_IMPLEMENTATION
 *       #include "sensor_device.h"
 *
 *   In every other file that needs the types and function declarations:
 *
 *       #include "sensor_device.h"        // no define here
 *
 * Defining SENSOR_DEVICE_IMPLEMENTATION in more than one .c file will cause
 * duplicate symbol linker errors.  Omitting it entirely will cause unresolved
 * symbol linker errors.
 *
 * INI integration (optional):
 * ===========================
 *
 *   sensor_device_load_ini() is compiled only when HAVE_CONFPARSER is defined:
 *
 *       #define HAVE_CONFPARSER
 *       #define SENSOR_DEVICE_IMPLEMENTATION
 *       #include "config/config_parser.h"   // must come before sensor_device.h
 *       #include "sensor_device.h"
 *
 *   The function accepts a pointer of type conf_result_t*, which is a typedef
 *   alias for the IniDoc type from config_parser.h.  The alias is defined here
 *   as a forward declaration so the header compiles without config_parser.h in
 *   files that only need the other functions.  Do NOT dereference or pass a
 *   conf_result_t* in any translation unit that has not included config_parser.h.
 *
 * Android sensor source:
 * ======================
 *
 *   When android_host is non-empty in DeviceDescriptor, the caller should
 *   connect to the Sensor Server app (github.com/umer0586/SensorServer) via
 *   WebSocket at:
 *     ws://<android_host>:<android_port>/sensor/connect?type=android.sensor.accelerometer
 *   Message format:
 *     {"type":"android.sensor.accelerometer","values":[ax,ay,az]}
 *
 * Supported pre-defined profiles: BMI160, MPU6050, ADXL345, generic.
 */

#ifndef SENSOR_DEVICE_H
#define SENSOR_DEVICE_H

/* ---- Standard headers needed for the public API ---- */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * conf_result_t -- INI document handle alias.
 *
 * This is a typedef alias for struct IniDoc from config_parser.h.  Using a
 * forward declaration here means any translation unit can include sensor_device.h
 * without pulling in config_parser.h.  Only the translation unit that calls
 * sensor_device_load_ini() must include config_parser.h beforehand to have a
 * complete type; passing an opaque pointer through the rest of the code is safe.
 * ============================================================================ */
typedef struct IniDoc conf_result_t;  /* FIX: was commented out — type was undefined */

/* ============================================================================
 * SENSOR_DEVICE_API -- linkage qualifier for function definitions.
 *
 * When SENSOR_DEVICE_STATIC is defined before inclusion all functions are given
 * internal linkage (static).  This is useful when you want each translation
 * unit to have its own private copy and you explicitly do NOT want a single
 * shared definition (useful in single-file programs or test harnesses).
 *
 * Normal usage: leave SENSOR_DEVICE_STATIC undefined and define
 * SENSOR_DEVICE_IMPLEMENTATION in exactly one .c file.
 * ============================================================================ */
#ifdef SENSOR_DEVICE_STATIC
#  define SENSOR_DEVICE_API static
#else
#  define SENSOR_DEVICE_API
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */
#define DEVICE_MODEL_MAX    32
#define DEVICE_MODE_MAX     16
#define DEVICE_ANDROID_MAX 128

/* ============================================================================
 * DeviceDescriptor -- full specification of one accelerometer instance.
 * All acceleration values are in m/s^2 (SI, compatible with Android API).
 * ============================================================================ */
typedef struct {
    /* Sensor identification */
    char     model[DEVICE_MODEL_MAX];          /* "BMI160", "MPU6050", "ADXL345", "generic" */

    /* Measurement range */
    float    range_g;                          /* Full-scale range in g, e.g. 16.0 */
    float    range_ms2;                        /* Derived by sensor_device_init(): range_g * 9.80665 */

    /* ADC resolution */
    float    resolution_ms2;                   /* m/s^2 per LSB */
    bool     apply_quantization;               /* Round output to LSB grid when true */

    /* Noise model: noise floor = noise_density * 9.80665 * sqrt(bandwidth_hz) */
    float    noise_density_g_sqhz;             /* Spectral density in g/sqrt(Hz) */

    /* Zero-g offset (calibration bias) per axis in m/s^2 */
    float    zero_offset_x;
    float    zero_offset_y;
    float    zero_offset_z;

    /* Sampling rate limits */
    uint32_t min_delay_us;                     /* Minimum sampling interval in microseconds */
    uint32_t max_delay_us;                     /* Maximum sampling interval in microseconds */

    /* Reporting behaviour */
    char     reporting_mode[DEVICE_MODE_MAX];  /* "continuous", "on-change", "one-shot" */
    bool     wake_up;                          /* Sensor can wake the CPU from low-power state */

    /* Optional Android sensor source.
     * When android_host is non-empty, the caller should connect to the
     * Sensor Server WebSocket app instead of using sensor_device_read(). */
    char     android_host[DEVICE_ANDROID_MAX];
    uint16_t android_port;

    /* Internal runtime state -- do not access directly. */
    float    _noise_sigma;                     /* Pre-computed: noise_density * 9.80665 * sqrt(bw) */
    double   _rng_state;                       /* Xorshift64 PRNG state; seeded by sensor_device_init() */
} DeviceDescriptor;

/* ============================================================================
 * DeviceProfile -- pre-defined sensor presets loaded by sensor_device_preset().
 * ============================================================================ */
typedef enum {
    DEVICE_PROFILE_GENERIC = 0,
    DEVICE_PROFILE_BMI160  = 1,
    DEVICE_PROFILE_MPU6050 = 2,
    DEVICE_PROFILE_ADXL345 = 3
} DeviceProfile;

/* ============================================================================
 * Public API -- declarations.
 * Definitions live below the #ifdef SENSOR_DEVICE_IMPLEMENTATION guard.
 * ============================================================================ */

/* Fill *dev with safe generic defaults. Call before any other function. */
SENSOR_DEVICE_API void sensor_device_default(DeviceDescriptor* dev);

/* Overwrite *dev with datasheet values for a known sensor. */
SENSOR_DEVICE_API void sensor_device_preset(DeviceDescriptor* dev, DeviceProfile profile);

/*
 * Finalize the descriptor after all fields have been set.
 * Derives range_ms2 and _noise_sigma from range_g and noise_density_g_sqhz,
 * and seeds the PRNG.  Must be called before sensor_device_read().
 * bandwidth_hz is typically sample_rate_hz / 2 (Nyquist).
 */
SENSOR_DEVICE_API void sensor_device_init(DeviceDescriptor* dev, double bandwidth_hz);

/*
 * Load the [device] INI section into *dev.
 * Compiled only when HAVE_CONFPARSER is defined at build time.
 *
 * The caller must include config_parser.h BEFORE this header in the one
 * translation unit that calls this function.  Other translation units that
 * only need to pass a conf_result_t* pointer do not need config_parser.h.
 *
 * Returns true if at least one field was loaded.
 */
#ifdef HAVE_CONFPARSER
SENSOR_DEVICE_API bool sensor_device_load_ini(DeviceDescriptor* dev,
                                               const conf_result_t* ini);
#endif

/*
 * Generate one accelerometer sample (ax, ay, az) in m/s^2.
 * Applies noise, ADC quantization, and zero-offset.
 * gravity_z is the static gravity component on Z (9.80665 for flat surface).
 * seq is used to diversify the noise seed across packets.
 */
SENSOR_DEVICE_API void sensor_device_read(const DeviceDescriptor* dev,
                                           float    gravity_z,
                                           float*   ax,
                                           float*   ay,
                                           float*   az,
                                           uint32_t seq);

/* Print a human-readable summary of the descriptor to stdout. */
SENSOR_DEVICE_API void sensor_device_print(const DeviceDescriptor* dev);

/*
 * Estimated RMS noise floor in m/s^2 at the given sample rate.
 * Formula: noise_density_g * 9.80665 * sqrt(sample_rate_hz / 2)
 */
SENSOR_DEVICE_API float sensor_device_noise_floor_ms2(const DeviceDescriptor* dev,
                                                        float sample_rate_hz);

/*
 * Minimum detectable signal in m/s^2 (3-sigma detection threshold).
 * MDS = 3 * noise_floor_ms2
 */
SENSOR_DEVICE_API float sensor_device_mds_ms2(const DeviceDescriptor* dev,
                                                float sample_rate_hz);

/*
 * Recommended sample rate to capture vibrations up to max_freq_hz.
 * Applies Nyquist theorem with 20% safety margin: fs = 2.4 * max_freq_hz.
 */
SENSOR_DEVICE_API float sensor_device_nyquist_rate(float max_freq_hz);

/*
 * Smallest standard range (2/4/8/16 g) that fits peak_ms2 with 20% headroom.
 */
SENSOR_DEVICE_API float sensor_device_recommend_range_g(float peak_ms2);

/* ============================================================================
 * Implementation -- compiled in exactly ONE translation unit.
 *
 * To enable, put in one .c file:
 *
 *     #define SENSOR_DEVICE_IMPLEMENTATION
 *     #include "sensor_device.h"
 *
 * Optionally add HAVE_CONFPARSER and include config_parser.h first:
 *
 *     #define HAVE_CONFPARSER
 *     #define SENSOR_DEVICE_IMPLEMENTATION
 *     #include "config/config_parser.h"
 *     #include "sensor_device.h"
 * ============================================================================ */
#ifdef SENSOR_DEVICE_IMPLEMENTATION

/* ---- C runtime includes needed only by the implementation ---- */
#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200112L
#  define _DEFAULT_SOURCE  1
#endif

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SENSOR_DEVICE__GRAVITY 9.80665f

/* Xorshift64 PRNG -- period 2^64-1, no heap allocation required. */
static uint64_t sensor_device__xorshift64(uint64_t* state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >>  7;
    x ^= x << 17;
    *state = x;
    return x;
}

/*
 * Box-Muller transform: produces one Gaussian sample with mean=0, std=sigma.
 * Consumes two uniform samples from the PRNG.
 */
static float sensor_device__gauss(uint64_t* state, float sigma) {
    float u1, u2, mag;
    do {
        u1 = (float)(sensor_device__xorshift64(state) & 0xFFFFFF) / (float)0x1000000;
        u2 = (float)(sensor_device__xorshift64(state) & 0xFFFFFF) / (float)0x1000000;
        if (u1 <= 0.0f) u1 = 1e-7f;
    } while (u1 <= 0.0f);
    mag = sigma * sqrtf(-2.0f * logf(u1));
    return mag * cosf(2.0f * 3.14159265f * u2);
}

/* Round v to the nearest quantization step. */
static float sensor_device__quantize(float v, float step) {
    if (step <= 0.0f) return v;
    return roundf(v / step) * step;
}

/* ---- sensor_device_default ---- */
SENSOR_DEVICE_API void sensor_device_default(DeviceDescriptor* dev) {
    memset(dev, 0, sizeof(*dev));
    strncpy(dev->model,          "generic",    DEVICE_MODEL_MAX - 1);
    strncpy(dev->reporting_mode, "continuous", DEVICE_MODE_MAX  - 1);
    dev->range_g              = 16.0f;
    dev->resolution_ms2       = 0.004790957f; /* 32g / 65536 * 9.80665 -- 16-bit at +-16g */
    dev->noise_density_g_sqhz = 0.0003f;      /* 300 ug/sqrt(Hz) -- typical MEMS floor */
    dev->min_delay_us         = 2500;          /* 400 Hz */
    dev->max_delay_us         = 1000000;       /* 1 Hz */
    dev->wake_up              = false;
    dev->apply_quantization   = true;
    dev->android_port         = 8080;
}

/* ---- sensor_device_preset ---- */
SENSOR_DEVICE_API void sensor_device_preset(DeviceDescriptor* dev, DeviceProfile profile) {
    sensor_device_default(dev);
    switch (profile) {

    case DEVICE_PROFILE_BMI160:
        /* Bosch BMI160 datasheet BST-BMI160-DS000:
         *   Range +-16g, 16-bit ADC: resolution = 2*16*9.80665/65536 = 0.004790957 m/s^2/LSB
         *   Noise density: 300 ug/sqrt(Hz)
         *   Max ODR: 1600 Hz; Android-reported min_delay 2500 us = 400 Hz typical */
        strncpy(dev->model, "BMI160", DEVICE_MODEL_MAX - 1);
        dev->range_g              = 16.0f;
        dev->resolution_ms2       = 0.004790957f;
        dev->noise_density_g_sqhz = 0.0003f;
        dev->min_delay_us         = 2500;
        dev->max_delay_us         = 1000000;
        dev->wake_up              = false;
        break;

    case DEVICE_PROFILE_MPU6050:
        /* InvenSense MPU-6050 PS-MPU-6000A:
         *   Range +-16g, 16-bit ADC
         *   Noise density: 400 ug/sqrt(Hz)
         *   Max ODR: 1000 Hz */
        strncpy(dev->model, "MPU6050", DEVICE_MODEL_MAX - 1);
        dev->range_g              = 16.0f;
        dev->resolution_ms2       = 0.004790957f;
        dev->noise_density_g_sqhz = 0.0004f;
        dev->min_delay_us         = 1000;
        dev->max_delay_us         = 1000000;
        dev->wake_up              = false;
        break;

    case DEVICE_PROFILE_ADXL345:
        /* Analog Devices ADXL345 Rev.F:
         *   Full-resolution mode: 13-bit, fixed 3.9 mg/LSB regardless of range
         *   resolution_ms2 = 0.004g * 9.80665 = 0.039227; but spec says 3.9 mg/LSB -> 0.038267
         *   Using rounded 0.003923 m/s^2/LSB
         *   Noise density: 150 ug/sqrt(Hz)
         *   Max ODR: 3200 Hz */
        strncpy(dev->model, "ADXL345", DEVICE_MODEL_MAX - 1);
        dev->range_g              = 16.0f;
        dev->resolution_ms2       = 0.003923f;
        dev->noise_density_g_sqhz = 0.00015f;
        dev->min_delay_us         = 313;
        dev->max_delay_us         = 1000000;
        dev->wake_up              = false;
        break;

    default: /* DEVICE_PROFILE_GENERIC -- defaults already set above */
        break;
    }
}

/* ---- sensor_device_init ---- */
SENSOR_DEVICE_API void sensor_device_init(DeviceDescriptor* dev, double bandwidth_hz) {
    dev->range_ms2 = dev->range_g * SENSOR_DEVICE__GRAVITY;

    dev->_noise_sigma = (bandwidth_hz > 0.0)
        ? dev->noise_density_g_sqhz * SENSOR_DEVICE__GRAVITY * sqrtf((float)bandwidth_hz)
        : 0.0f;

    /* Seed PRNG from wall clock XOR a hash of the model string. */
    uint64_t seed = (uint64_t)time(NULL) ^ 0xDEADBEEFCAFEBABEULL;
    for (int i = 0; dev->model[i] && i < DEVICE_MODEL_MAX; ++i)
        seed = seed * 31u + (unsigned char)dev->model[i];
    dev->_rng_state = (double)(seed | 1ULL);
}

/* ---- sensor_device_load_ini ---- */
#ifdef HAVE_CONFPARSER
SENSOR_DEVICE_API bool sensor_device_load_ini(DeviceDescriptor* dev,
                                               const conf_result_t* ini)
{
    /* conf_result_t is typedef struct IniDoc conf_result_t (see top of file).
     * The cast to const IniDoc* is a no-op because they are the same type.
     * config_parser.h must be included by the caller before this header in
     * the translation unit that actually calls this function. */
    const IniDoc* doc = (const IniDoc*)ini;  /* FIX: was commented out — doc was undefined */
    if (!doc) return false;
    bool loaded = false;

    const char* model = ini_get_str(doc, "device", "model", NULL);
    if (model) {
        if      (strcmp(model, "BMI160")  == 0) sensor_device_preset(dev, DEVICE_PROFILE_BMI160);
        else if (strcmp(model, "MPU6050") == 0) sensor_device_preset(dev, DEVICE_PROFILE_MPU6050);
        else if (strcmp(model, "ADXL345") == 0) sensor_device_preset(dev, DEVICE_PROFILE_ADXL345);
        else {
            strncpy(dev->model, model, DEVICE_MODEL_MAX - 1);
            dev->model[DEVICE_MODEL_MAX - 1] = '\0';
        }
        loaded = true;
    }

    if (ini_has_key(doc, "device", "range_g")) {
        dev->range_g = (float)ini_get_double(doc, "device", "range_g", dev->range_g);
        loaded = true;
    }
    if (ini_has_key(doc, "device", "resolution")) {
        dev->resolution_ms2 = (float)ini_get_double(doc, "device", "resolution",
                                                     dev->resolution_ms2);
        loaded = true;
    }
    if (ini_has_key(doc, "device", "noise_density")) {
        dev->noise_density_g_sqhz = (float)ini_get_double(doc, "device", "noise_density",
                                                           dev->noise_density_g_sqhz);
        loaded = true;
    }
    if (ini_has_key(doc, "device", "min_delay_us")) {
        dev->min_delay_us = ini_get_uint32(doc, "device", "min_delay_us", dev->min_delay_us);
        loaded = true;
    }
    if (ini_has_key(doc, "device", "max_delay_us")) {
        dev->max_delay_us = ini_get_uint32(doc, "device", "max_delay_us", dev->max_delay_us);
        loaded = true;
    }
    if (ini_has_key(doc, "device", "reporting_mode")) {
        const char* rm = ini_get_str(doc, "device", "reporting_mode", dev->reporting_mode);
        strncpy(dev->reporting_mode, rm, DEVICE_MODE_MAX - 1);
        loaded = true;
    }
    if (ini_has_key(doc, "device", "wake_up")) {
        dev->wake_up = ini_get_bool(doc, "device", "wake_up", dev->wake_up);
        loaded = true;
    }
    if (ini_has_key(doc, "device", "quantization")) {
        dev->apply_quantization = ini_get_bool(doc, "device", "quantization",
                                                dev->apply_quantization);
        loaded = true;
    }
    if (ini_has_key(doc, "device", "offset_x")) {
        dev->zero_offset_x = (float)ini_get_double(doc, "device", "offset_x", 0.0);
        loaded = true;
    }
    if (ini_has_key(doc, "device", "offset_y")) {
        dev->zero_offset_y = (float)ini_get_double(doc, "device", "offset_y", 0.0);
        loaded = true;
    }
    if (ini_has_key(doc, "device", "offset_z")) {
        dev->zero_offset_z = (float)ini_get_double(doc, "device", "offset_z", 0.0);
        loaded = true;
    }
    const char* ahost = ini_get_str(doc, "device", "android_host", NULL);
    if (ahost && ahost[0] != '\0') {
        strncpy(dev->android_host, ahost, DEVICE_ANDROID_MAX - 1);
        dev->android_port = ini_get_uint16(doc, "device", "android_port", 8080);
        loaded = true;
    }

    return loaded;
}
#endif /* HAVE_CONFPARSER */

/* ---- sensor_device_read ---- */
SENSOR_DEVICE_API void sensor_device_read(const DeviceDescriptor* dev,
                                           float    gravity_z,
                                           float*   ax,
                                           float*   ay,
                                           float*   az,
                                           uint32_t seq)
{
    /* Mutate the internal PRNG state through a const-discarding cast.
     * _rng_state is logically mutable (lazy state); this is the only mutation. */
    DeviceDescriptor* mdev = (DeviceDescriptor*)(uintptr_t)dev;
    uint64_t* rng = (uint64_t*)&mdev->_rng_state;

    /* Mix seq into the state so each packet gets different noise. */
    uint64_t state = *rng ^ ((uint64_t)seq * 0x9E3779B97F4A7C15ULL);
    if (state == 0) state = 1;

    float nx = sensor_device__gauss(&state, dev->_noise_sigma);
    float ny = sensor_device__gauss(&state, dev->_noise_sigma);
    float nz = sensor_device__gauss(&state, dev->_noise_sigma);

    *ax = dev->zero_offset_x + nx;
    *ay = dev->zero_offset_y + ny;
    *az = gravity_z + dev->zero_offset_z + nz;

    /* Clamp to measurement range. */
    float lim = dev->range_ms2;
    if (*ax >  lim) *ax =  lim;
    if (*ax < -lim) *ax = -lim;
    if (*ay >  lim) *ay =  lim;
    if (*ay < -lim) *ay = -lim;
    if (*az >  lim) *az =  lim;
    if (*az < -lim) *az = -lim;

    if (dev->apply_quantization && dev->resolution_ms2 > 0.0f) {
        *ax = sensor_device__quantize(*ax, dev->resolution_ms2);
        *ay = sensor_device__quantize(*ay, dev->resolution_ms2);
        *az = sensor_device__quantize(*az, dev->resolution_ms2);
    }

    *rng = state;
}

/* ---- sensor_device_print ---- */
SENSOR_DEVICE_API void sensor_device_print(const DeviceDescriptor* dev) {
    float rms2 = (dev->range_ms2 > 0.0f) ? dev->range_ms2
                                          : dev->range_g * SENSOR_DEVICE__GRAVITY;
    printf("[device] Model           : %s\n",  dev->model);
    printf("[device] Range           : +/-%.1f g  (+/-%.4f m/s^2)\n", dev->range_g, rms2);
    printf("[device] Resolution      : %.9f m/s^2/LSB\n", dev->resolution_ms2);
    printf("[device] Noise density   : %.6f g/sqrt(Hz)  (%.3f mg/sqrt(Hz))\n",
           dev->noise_density_g_sqhz, dev->noise_density_g_sqhz * 1000.0f);
    printf("[device] Min delay       : %u us  (%.1f Hz max)\n",
           dev->min_delay_us, 1e6f / (float)dev->min_delay_us);
    printf("[device] Max delay       : %u us  (%.3f Hz min)\n",
           dev->max_delay_us, 1e6f / (float)dev->max_delay_us);
    printf("[device] Reporting mode  : %s\n",  dev->reporting_mode);
    printf("[device] Wake-up         : %s\n",  dev->wake_up ? "true" : "false");
    printf("[device] Quantization    : %s\n",  dev->apply_quantization ? "on" : "off");
    if (dev->android_host[0])
        printf("[device] Android source  : ws://%s:%u\n",
               dev->android_host, dev->android_port);
}

/* ---- Noise analysis ---- */
SENSOR_DEVICE_API float sensor_device_noise_floor_ms2(const DeviceDescriptor* dev,
                                                        float sample_rate_hz) {
    return dev->noise_density_g_sqhz * SENSOR_DEVICE__GRAVITY * sqrtf(sample_rate_hz / 2.0f);
}

SENSOR_DEVICE_API float sensor_device_mds_ms2(const DeviceDescriptor* dev,
                                                float sample_rate_hz) {
    return 3.0f * sensor_device_noise_floor_ms2(dev, sample_rate_hz);
}

SENSOR_DEVICE_API float sensor_device_nyquist_rate(float max_freq_hz) {
    return 2.4f * max_freq_hz; /* Nyquist minimum * 1.2 safety margin */
}

SENSOR_DEVICE_API float sensor_device_recommend_range_g(float peak_ms2) {
    float needed_g = (peak_ms2 / SENSOR_DEVICE__GRAVITY) * 1.2f; /* 20% headroom */
    if (needed_g <= 2.0f)  return  2.0f;
    if (needed_g <= 4.0f)  return  4.0f;
    if (needed_g <= 8.0f)  return  8.0f;
    return 16.0f;
}

#undef SENSOR_DEVICE__GRAVITY

#endif /* SENSOR_DEVICE_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_DEVICE_H */

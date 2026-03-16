/*
 * sensor/sensor_device.c
 * Accelerometer device emulation with configurable noise and quantization.
 * Standard: C11.
 */

#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200112L
#  define _DEFAULT_SOURCE  1
#endif

#include "sensor_device.h"
#include <confparser.h>

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Standard gravity constant (m/s^2) */
#define GRAVITY_MS2  9.80665f

/* Xorshift64 pseudo-random number generator — period 2^64-1, no malloc */
static uint64_t xorshift64(uint64_t* state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* Map xorshift64 output to float in [-1, 1] */

/*
 * Box-Muller transform: generates Gaussian noise with zero mean and sigma=1.
 * Uses two uniform samples to produce one Gaussian sample.
 */
static float gauss_noise(uint64_t* state, float sigma) {
    float u1, u2, mag;
    do {
        u1 = (float)(xorshift64(state) & 0xFFFFFF) / (float)0x1000000;
        u2 = (float)(xorshift64(state) & 0xFFFFFF) / (float)0x1000000;
        u1 = u1 > 0.0f ? u1 : 1e-7f;
    } while (u1 <= 0.0f);
    mag = sigma * sqrtf(-2.0f * logf(u1));
    return mag * cosf(2.0f * 3.14159265f * u2);
}

/* Apply ADC quantization: round to nearest resolution step */
static float quantize(float v, float resolution) {
    if (resolution <= 0.0f) return v;
    return roundf(v / resolution) * resolution;
}

/* --------------------------------------------------------------------------- */
/* Built-in profiles */
/* --------------------------------------------------------------------------- */

void sensor_device_default(DeviceDescriptor* dev) {
    memset(dev, 0, sizeof(*dev));
    strncpy(dev->model,          "generic",    DEVICE_MODEL_MAX - 1);
    strncpy(dev->reporting_mode, "continuous", DEVICE_MODE_MAX  - 1);
    dev->range_g               = 16.0f;
    dev->resolution_ms2        = 0.004790957f; /* ±16g, 16-bit: 32g/65536*9.80665 */
    dev->noise_density_g_sqhz  = 0.0003f;      /* 300 ug/sqrt(Hz) typical MEMS */
    dev->zero_offset_x         = 0.0f;
    dev->zero_offset_y         = 0.0f;
    dev->zero_offset_z         = 0.0f;
    dev->min_delay_us          = 2500;          /* 400 Hz */
    dev->max_delay_us          = 1000000;       /* 1 Hz  */
    dev->wake_up               = false;
    dev->apply_quantization    = true;
    dev->android_port          = 8080;
}

void sensor_device_preset(DeviceDescriptor* dev, DeviceProfile profile) {
    sensor_device_default(dev);
    switch (profile) {
    case DEVICE_PROFILE_BMI160:
        /*
         * Bosch BMI160 datasheet parameters:
         *   Range options: ±2/±4/±8/±16g — using ±16g (max range mode)
         *   Resolution: 16-bit ADC → (2*16*9.80665)/65536 = 0.004790957 m/s^2/LSB
         *   Noise density: 300 ug/sqrt(Hz) = 0.0003 g/sqrt(Hz)
         *   Max ODR: 1600 Hz (min_delay = 625 us), typical use 400 Hz
         *   Android-reported min_delay: 2500 us = 400 Hz
         */
        strncpy(dev->model, "BMI160", DEVICE_MODEL_MAX - 1);
        dev->range_g              = 16.0f;
        dev->resolution_ms2       = 0.004790957f;
        dev->noise_density_g_sqhz = 0.0003f;
        dev->min_delay_us         = 2500;
        dev->max_delay_us         = 1000000;
        dev->wake_up              = false;
        break;

    case DEVICE_PROFILE_MPU6050:
        /*
         * InvenSense MPU-6050:
         *   Range: ±16g, 16-bit ADC
         *   Noise density: 400 ug/sqrt(Hz)
         *   Max ODR: 1000 Hz (min_delay = 1000 us)
         */
        strncpy(dev->model, "MPU6050", DEVICE_MODEL_MAX - 1);
        dev->range_g              = 16.0f;
        dev->resolution_ms2       = 0.004790957f;
        dev->noise_density_g_sqhz = 0.0004f;
        dev->min_delay_us         = 1000;
        dev->max_delay_us         = 1000000;
        dev->wake_up              = false;
        break;

    case DEVICE_PROFILE_ADXL345:
        /*
         * Analog Devices ADXL345:
         *   Range: ±16g, 13-bit ADC in full-resolution mode
         *   Resolution fixed at 3.9 mg/LSB regardless of range
         *   Noise density: 150 ug/sqrt(Hz) (lower noise than BMI160)
         *   Max ODR: 3200 Hz (min_delay = 312 us)
         */
        strncpy(dev->model, "ADXL345", DEVICE_MODEL_MAX - 1);
        dev->range_g              = 16.0f;
        dev->resolution_ms2       = 0.003923f;   /* 0.004g * 9.80665 / 1.0 ≈ 0.004g */
        dev->noise_density_g_sqhz = 0.00015f;    /* 150 ug/sqrt(Hz) */
        dev->min_delay_us         = 313;          /* 3200 Hz */
        dev->max_delay_us         = 1000000;
        dev->wake_up              = false;
        break;

    default: /* GENERIC */
        break;
    }
}

void sensor_device_init(DeviceDescriptor* dev, double bandwidth_hz) {
    /* Derive range in m/s^2 */
    dev->range_ms2 = dev->range_g * GRAVITY_MS2;

    /* Pre-compute noise sigma for the given measurement bandwidth */
    if (bandwidth_hz > 0.0) {
        dev->_noise_sigma = dev->noise_density_g_sqhz
                          * GRAVITY_MS2
                          * sqrtf((float)bandwidth_hz);
    } else {
        dev->_noise_sigma = 0.0f;
    }

    /* Seed PRNG with time + model name hash to get different noise per run */
    uint64_t seed = (uint64_t)time(NULL) ^ 0xDEADBEEFCAFEBABEULL;
    for (int i = 0; dev->model[i] && i < DEVICE_MODEL_MAX; ++i)
        seed = seed * 31 + (unsigned char)dev->model[i];
    dev->_rng_state = (double)(seed | 1ULL); /* ensure non-zero */
}


bool sensor_device_load_conf(DeviceDescriptor* dev, const conf_result_t* conf) {
    if (!conf) return false;
    bool loaded = false;

    const char* model = conf_get_str(conf, "device", "model", NULL);
    if (model) {
        /* Check for known profiles first */
        if (strcmp(model, "BMI160")  == 0) sensor_device_preset(dev, DEVICE_PROFILE_BMI160);
        else if (strcmp(model, "MPU6050") == 0) sensor_device_preset(dev, DEVICE_PROFILE_MPU6050);
        else if (strcmp(model, "ADXL345") == 0) sensor_device_preset(dev, DEVICE_PROFILE_ADXL345);
        else {
            strncpy(dev->model, model, DEVICE_MODEL_MAX - 1);
            dev->model[DEVICE_MODEL_MAX - 1] = '\0';
        }
        loaded = true;
    }

    /* Override individual fields (allow partial override of presets) */
    if (conf_has_key(conf, "device", "range_g")) {
        dev->range_g = (float)conf_get_double(conf, "device", "range_g", dev->range_g);
        loaded = true;
    }
    if (conf_has_key(conf, "device", "resolution")) {
        dev->resolution_ms2 = (float)conf_get_double(conf, "device", "resolution",
                                                      dev->resolution_ms2);
        loaded = true;
    }
    if (conf_has_key(conf, "device", "noise_density")) {
        dev->noise_density_g_sqhz = (float)conf_get_double(conf, "device", "noise_density",
                                                            dev->noise_density_g_sqhz);
        loaded = true;
    }
    if (conf_has_key(conf, "device", "min_delay_us")) {
        dev->min_delay_us = conf_get_uint32(conf, "device", "min_delay_us", dev->min_delay_us);
        loaded = true;
    }
    if (conf_has_key(conf, "device", "max_delay_us")) {
        dev->max_delay_us = conf_get_uint32(conf, "device", "max_delay_us", dev->max_delay_us);
        loaded = true;
    }
    if (conf_has_key(conf, "device", "reporting_mode")) {
        const char* rm = conf_get_str(conf, "device", "reporting_mode", dev->reporting_mode);
        strncpy(dev->reporting_mode, rm, DEVICE_MODE_MAX - 1);
        loaded = true;
    }
    if (conf_has_key(conf, "device", "wake_up")) {
        dev->wake_up = conf_get_bool(conf, "device", "wake_up", dev->wake_up);
        loaded = true;
    }
    if (conf_has_key(conf, "device", "quantization")) {
        dev->apply_quantization = conf_get_bool(conf, "device", "quantization",
                                                dev->apply_quantization);
        loaded = true;
    }
    if (conf_has_key(conf, "device", "offset_x")) {
        dev->zero_offset_x = (float)conf_get_double(conf, "device", "offset_x", 0.0);
        loaded = true;
    }
    if (conf_has_key(conf, "device", "offset_y")) {
        dev->zero_offset_y = (float)conf_get_double(conf, "device", "offset_y", 0.0);
        loaded = true;
    }
    if (conf_has_key(conf, "device", "offset_z")) {
        dev->zero_offset_z = (float)conf_get_double(conf, "device", "offset_z", 0.0);
        loaded = true;
    }

    /* Optional Android sensor source */
    const char* ahost = conf_get_str(conf, "device", "android_host", NULL);
    if (ahost && ahost[0] != '\0') {
        strncpy(dev->android_host, ahost, DEVICE_ANDROID_MAX - 1);
        dev->android_port = conf_get_uint16(conf, "device", "android_port", 8080);
        loaded = true;
    }

    return loaded;
}

/* --------------------------------------------------------------------------- */
/* Sampling */
/* --------------------------------------------------------------------------- */

void sensor_device_read(const DeviceDescriptor* dev,
                          float gravity_z,
                          float* ax, float* ay, float* az,
                          uint32_t seq)
{
    /* Mutable copy of RNG state (const_cast equivalent in C) */
    DeviceDescriptor* mdev = (DeviceDescriptor*)(uintptr_t)dev;
    uint64_t* rng = (uint64_t*)&mdev->_rng_state;
    /* seed with seq to ensure reproducible per-packet noise */
    uint64_t state = *rng ^ ((uint64_t)seq * 0x9E3779B97F4A7C15ULL);
    if (state == 0) state = 1;

    float noise_x = gauss_noise(&state, dev->_noise_sigma);
    float noise_y = gauss_noise(&state, dev->_noise_sigma);
    float noise_z = gauss_noise(&state, dev->_noise_sigma);

    *ax = 0.0f + dev->zero_offset_x + noise_x;
    *ay = 0.0f + dev->zero_offset_y + noise_y;
    *az = gravity_z + dev->zero_offset_z + noise_z;

    /* Clamp to measurement range */
    float limit = dev->range_ms2;
    if (*ax >  limit) *ax =  limit;
    if (*ax < -limit) *ax = -limit;
    if (*ay >  limit) *ay =  limit;
    if (*ay < -limit) *ay = -limit;
    if (*az >  limit) *az =  limit;
    if (*az < -limit) *az = -limit;

    /* Apply ADC quantization */
    if (dev->apply_quantization && dev->resolution_ms2 > 0.0f) {
        *ax = quantize(*ax, dev->resolution_ms2);
        *ay = quantize(*ay, dev->resolution_ms2);
        *az = quantize(*az, dev->resolution_ms2);
    }

    /* Update persistent RNG state */
    *rng = state;
}

/* --------------------------------------------------------------------------- */
/* Diagnostics */
/* --------------------------------------------------------------------------- */

void sensor_device_print(const DeviceDescriptor* dev) {
    float range_ms2 = dev->range_ms2 > 0.0f ? dev->range_ms2 : dev->range_g * 9.80665f;
    printf("[device] Model           : %s\n",   dev->model);
    printf("[device] Range           : ±%.1f g  (±%.4f m/s^2)\n",
           dev->range_g, range_ms2);
    printf("[device] Resolution      : %.9f m/s^2/LSB\n", dev->resolution_ms2);
    printf("[device] Noise density   : %.6f g/sqrt(Hz)  (%.6f mg/sqrt(Hz))\n",
           dev->noise_density_g_sqhz, dev->noise_density_g_sqhz * 1000.0f);
    printf("[device] Min delay       : %u us  (%.1f Hz max)\n",
           dev->min_delay_us, 1e6f / (float)dev->min_delay_us);
    printf("[device] Max delay       : %u us  (%.3f Hz min)\n",
           dev->max_delay_us, 1e6f / (float)dev->max_delay_us);
    printf("[device] Reporting mode  : %s\n",   dev->reporting_mode);
    printf("[device] Wake-up sensor  : %s\n",   dev->wake_up ? "true" : "false");
    printf("[device] Quantization    : %s\n",   dev->apply_quantization ? "on" : "off");
    if (dev->android_host[0])
        printf("[device] Android source  : ws://%s:%u\n",
               dev->android_host, dev->android_port);
}

float sensor_device_noise_floor_ms2(const DeviceDescriptor* dev, float sample_rate_hz) {
    /* Nyquist bandwidth = sample_rate / 2 */
    float bw = sample_rate_hz / 2.0f;
    return dev->noise_density_g_sqhz * GRAVITY_MS2 * sqrtf(bw);
}

float sensor_device_mds_ms2(const DeviceDescriptor* dev, float sample_rate_hz) {
    return 3.0f * sensor_device_noise_floor_ms2(dev, sample_rate_hz);
}

float sensor_device_nyquist_rate(float max_freq_hz) {
    /* 20% safety margin above theoretical minimum */
    return 2.4f * max_freq_hz;
}

float sensor_device_recommend_range_g(float peak_ms2) {
    float peak_g = peak_ms2 / GRAVITY_MS2;
    float needed_g = peak_g * 1.2f; /* 20% headroom */
    /* Standard IMU range options: 2, 4, 8, 16 g */
    if (needed_g <= 2.0f)  return 2.0f;
    if (needed_g <= 4.0f)  return 4.0f;
    if (needed_g <= 8.0f)  return 8.0f;
    return 16.0f;
}

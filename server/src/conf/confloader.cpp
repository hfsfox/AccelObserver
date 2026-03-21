#include <conf/confloader.hpp>

#ifdef HAVE_CONFPARSER

#include <logger/logger.hpp>
#include <cctype>
#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// str_to_lower_copy  — lowercase a C string into a std::string.
// Used to compare config values case-insensitively (confparser lowercases
// keys but leaves values in their original case).
// ---------------------------------------------------------------------------
static std::string str_to_lower_copy(const char* s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return out;
}

// ---------------------------------------------------------------------------
// safe_port — returns the value if it is a valid TCP port [1, 65535],
// or the fallback when the config key is absent / zero / out of range.
// conf_get_uint16 does not reject port 0, so we add that check here.
// ---------------------------------------------------------------------------
static uint16_t safe_port(const conf_result_t* conf,
                           const char* section, const char* key,
                           uint16_t fallback)
{
    uint16_t v = conf_get_uint16(conf, section, key, 0);
    if (v == 0) {
        // Key absent (conf_get_uint16 returned our explicit 0 default) OR
        // key present with value 0 — either way keep the existing setting.
        // Re-check: if the key exists with value 0 that is still invalid.
        if (!conf_has_key(conf, section, key)) return fallback;
        LOG_WARNF("[config] [%s] %s = 0 is not a valid port, keeping %u",
                  section, key, (unsigned)fallback);
        return fallback;
    }
    return v;
}

void
apply_conf(const conf_result_t* conf, server::Config& cfg)
{
    using T = server::TransportType;

    // ---- [transport] -------------------------------------------------------
    const char* mode = conf_get_str(conf, "transport", "protocol", nullptr);
    if (mode && mode[0]) {
        // confparser lowercases keys but NOT values — compare case-insensitively.
        std::string m = str_to_lower_copy(mode);
        if      (m == "mqtt")                   cfg.transport = T::MQTT;
        else if (m == "ws" || m == "websocket") cfg.transport = T::WebSocket;
        else LOG_WARNF("[config] [transport] protocol='%s' unknown (use mqtt|ws)", mode);
    }
    // webinterface flag in [transport] section (convenience alias for [web] enabled).
    if (conf_has_key(conf, "transport", "webinterface"))
        cfg.web_enabled = conf_get_bool(conf, "transport", "webinterface", false);

    // ---- [connection] ------------------------------------------------------
    //const char* h = conf_get_str(conf, "connection", "host", nullptr);
    std::string h = conf_get_str(conf, "connection", "host", nullptr);

    // Guard: empty host= in config must not replace a valid default with "".
    /*if (h && h[0])*/
    if(!h.empty()) cfg.host = h;

    cfg.port = safe_port(conf, "connection", "port", cfg.port);

    // ---- [mqtt] ------------------------------------------------------------
    const char* t = conf_get_str(conf, "mqtt", "topic", nullptr);
    // Guard: empty topic= must not clear a valid default.
    if (t && t[0]) cfg.mqtt_topic = t;

    const char* cid = conf_get_str(conf, "mqtt", "client_id", nullptr);
    // Empty client_id is intentionally allowed — libmosquitto generates one.
    if (cid) cfg.mqtt_client_id = cid;

    if (conf_has_key(conf, "mqtt", "username"))
        cfg.mqtt_username = conf_get_str(conf, "mqtt", "username", "");
    if (conf_has_key(conf, "mqtt", "password"))
        cfg.mqtt_password = conf_get_str(conf, "mqtt", "password", "");

    {
        int qos = conf_get_int(conf, "mqtt", "qos", cfg.mqtt_qos);
        if (qos < 0 || qos > 2) {
            LOG_WARNF("[config] mqtt.qos=%d out of range (0-2), using 0", qos);
            qos = 0;
        }
        cfg.mqtt_qos = qos;
    }
    cfg.mqtt_keepalive = conf_get_int(conf, "mqtt", "keepalive", cfg.mqtt_keepalive);

    // ---- [mqtt] Last Will and Testament ------------------------------------
    const char* wt = conf_get_str(conf, "mqtt", "will_topic", nullptr);
    if (wt && wt[0]) cfg.mqtt_will_topic = wt;

    const char* wp = conf_get_str(conf, "mqtt", "will_payload", nullptr);
    if (wp && wp[0]) cfg.mqtt_will_payload = wp;

    if (conf_has_key(conf, "mqtt", "will_qos")) {
        int wqos = conf_get_int(conf, "mqtt", "will_qos", cfg.mqtt_will_qos);
        if (wqos < 0 || wqos > 2) {
            LOG_WARNF("[config] mqtt.will_qos=%d out of range (0-2), using 0", wqos);
            wqos = 0;
        }
        cfg.mqtt_will_qos = wqos;
    }
    if (conf_has_key(conf, "mqtt", "will_retain"))
        cfg.mqtt_will_retain = conf_get_bool(conf, "mqtt", "will_retain",
                                             cfg.mqtt_will_retain);

    // ---- [storage] ---------------------------------------------------------
    const char* out = conf_get_str(conf, "storage", "output", nullptr);
    if (out && out[0]) cfg.output_file = out;
    const char* sp = conf_get_str(conf, "storage", "store_path", nullptr);
    if (sp && sp[0]) cfg.store_path = sp;

    if (conf_has_key(conf, "storage", "buffer_capacity")) {
        std::size_t cap = conf_get_size(conf, "storage", "buffer_capacity", 0);
        if (cap == 0) {
            LOG_WARN("[config] storage.buffer_capacity=0 is invalid, keeping current value");
        } else {
            cfg.buffer_capacity = cap;
            cfg.auto_buffer = false; // explicit value disables auto-sizing
        }
    }

    {
        std::size_t fi = conf_get_size(conf, "storage", "flush_interval_ms",
                                       cfg.flush_interval_ms);
        if (fi == 0) {
            LOG_WARN("[config] storage.flush_interval_ms=0 would cause a busy-loop, keeping current value");
        } else {
            cfg.flush_interval_ms = fi;
        }
    }

    const char* sep = conf_get_str(conf, "storage", "csv_separator", nullptr);
    if (sep && sep[0]) cfg.csv_separator = sep;

    // ---- [logging] ---------------------------------------------------------
    const char* lf = conf_get_str(conf, "logging", "file", nullptr);
    // Guard: empty file= must not override the valid "stderr" default with "".
    if (lf && lf[0]) cfg.log_file = lf;

    cfg.log_also_stderr = conf_get_bool(conf, "logging", "also_stderr",
                                        cfg.log_also_stderr);

    const char* ll = conf_get_str(conf, "logging", "level", nullptr);
    if (ll && ll[0]) cfg.log_level = ll;

    // ---- [web] -------------------------------------------------------------
    if (conf_has_key(conf, "web", "enabled"))
        cfg.web_enabled = conf_get_bool(conf, "web", "enabled", cfg.web_enabled);

    const char* wh = conf_get_str(conf, "web", "host", nullptr);
    if (wh && wh[0]) cfg.web_host = wh;

    cfg.web_port = safe_port(conf, "web", "port", cfg.web_port);

    cfg.web_stats_interval_ms = conf_get_uint32(conf, "web", "stats_interval_ms",
                                                cfg.web_stats_interval_ms);

    // ---- [device] ----------------------------------------------------------
    const char* dm = conf_get_str(conf, "device", "model", nullptr);
    if (dm && dm[0]) cfg.device_model = dm;
    if (conf_has_key(conf, "device", "range_g"))
        cfg.device_range_g = (float)conf_get_double(conf, "device", "range_g",
                                                    (double)cfg.device_range_g);

    // ---- [validator] -------------------------------------------------------
    const char* ts_str = conf_get_str(conf, "validator", "timesource", nullptr);
    if (ts_str && ts_str[0]) cfg.timesource = ts_str;
    if (conf_has_key(conf, "validator", "max_acc_ms2"))
        cfg.max_acc_ms2 = conf_get_double(conf, "validator", "max_acc_ms2",
                                          cfg.max_acc_ms2);
    if (conf_has_key(conf, "validator", "seq_optional"))
        cfg.seq_optional = conf_get_bool(conf, "validator", "seq_optional",
                                         cfg.seq_optional);
}

#endif

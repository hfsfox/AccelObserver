#ifndef __SERVERTYPES_H__
#define __SERVERTYPES_H__

#pragma once

//Core data structures and unified server configuration.

#include <cstdint>
#include <string>

namespace server
{

    struct DataPacket
    {
        uint64_t timestamp_ms;   // client-side Unix timestamp in milliseconds
        uint32_t sequence_id;    // monotonically increasing per-session counter
        double   acc_x;          // m/s^2
        double   acc_y;          // m/s^2
        double   acc_z;          // m/s^2
    };

    enum class ParseResult
    {
        OK,
        INVALID_FORMAT,
        MISSING_FIELDS,
        OUT_OF_RANGE
    };

    struct ParsedPacket
    {
        DataPacket  packet;
        ParseResult result;
    };

    enum class TransportType
    {
        MQTT,
        WebSocket
    };

    struct Config
    {
        //transport protocol settings
        // CLI:  --protocol ws | mqtt
        // conf: [transport] protocol = ws | mqtt
        TransportType transport      = TransportType::MQTT;

        // Host: MQTT broker address or WebSocket bind address
        std::string   host           = "localhost";
        uint16_t      port           = 1883;

        // MQTT-specific fields
        std::string   mqtt_topic     = "sensors/accel";
        std::string   mqtt_client_id = "data-subscriber";
        std::string   mqtt_username  = "";
        std::string   mqtt_password  = "";
        int           mqtt_qos       = 0;
        int           mqtt_keepalive = 60;

        // storage settings
        // output_file: exact csv path or auto-generated default format name on store_path address
        // store_path:  directory for auto generated csv data (trailing separator added if missing)
        // cli:  --output and storepath
        // conf: [storage] putput store_path
        std::string   output_file;
        std::string   store_path;

        //   Buffer capacity and flush interval are coupled:
        //
        //  minimum safe buffer = rate_hz * flush_interval_ms / 1000
        //
        //  A packet arriving while the buffer is full is dropped.  The flush thread
        //  drains the buffer every flush_interval_ms.  If the buffer holds fewer
        //  packets than arrive during one flush interval the overflow is lost.
        //
        //  Example: 100 Hz, 500 ms flush -> minimum buffer = 50 packets.
        //  Recommended safety factor: 2x -> buffer = 100 packets.
        //
        // When auto_buffer is true (default), main.cpp calculates buffer_capacity
        // from rate_hz and flush_interval_ms using the 2x formula.
        // An explicit --buf argument disables auto-sizing.

        std::size_t   buffer_capacity    = 4096;
        std::size_t   flush_interval_ms  = 500;
        bool          auto_buffer        = true;  // recalculate from rate when true

        // Logging
        std::string   log_file       = "stderr";
        bool          log_also_stderr = true;
        std::string   log_level      = "INFO";

        /*
        * Real-time browser visualization.
        * CLI:  --webiface true|false
        * INI:  [transport] webinterface = true  OR  [web] enabled = true
        */
        bool          web_enabled    = false;
        std::string   web_host       = "0.0.0.0";
        uint16_t      web_port       = 8088;
        uint32_t      web_stats_interval_ms = 1000;

        // Device model name and range (from [device] section, display and range validation)
        std::string   device_model   = "generic";
        float         device_range_g = 16.0f;

        // Packet validator options.
        // timesource:   "external" = ts required in every packet (default);
        //               "host"     = server clock used when ts is absent or zero.
        // CLI:  --timesource external|host
        // conf:  [validator] timesource = external | host
        //
        // max_acc_ms2: acceleration range limit for OUT_OF_RANGE detection.
        // If zero, the value from device_range_g * 9.80665 is used automatically.
        // CLI:  --max-acc <value>
        // conf:  [validator] max_acc_ms2 = <value>
        //
        // seq_optional: when true, packets without a seq field are accepted
        // with sequence_id defaulting to 0.
        // conf:  [validator] seq_optional = true | false
        //

        std::string   timesource      = "external";
        double        max_acc_ms2     = 0.0;       // 0 = derive from device_range_g
        bool          seq_optional    = false;
    };

} /* namespace subscriber */

#endif /*__SERVERTYPES_H__*/

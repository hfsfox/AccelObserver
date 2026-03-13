#ifndef __HELP_HPP__
#define __HELP_HPP__

#include <iostream>

void print_usage(const char* prog)
{
    std::cout
    << "Usage: " << prog << " [OPTIONS]\n\n"
    << "Config file:\n"
    << "  --config <path>       INI config file path\n"
    << "                        Default search:\n"
    << "                          ./data_subscriber.ini\n"
    << "                          ~/.config/data_subscriber/data_subscriber.ini\n"
    << "                          /etc/data_subscriber/data_subscriber.ini\n"
    << "  --dump-config         Print resolved configuration and exit\n\n"
    << "Transport:\n"
    << "  --ws                  WebSocket server mode (default)\n"
    << "  --mqtt                MQTT subscriber mode\n\n"
    << "Connection ([connection]):\n"
    << "  --host <str>          MQTT broker host (default: localhost)\n"
    << "  --port <int>          Port: WS listen / MQTT connect (default: 8080)\n\n"
    << "MQTT ([mqtt]):\n"
    << "  --topic <str>         MQTT topic, wildcards ok (default: sensors/accel)\n"
    << "  --mqtt-id <str>       MQTT client ID (default: data-subscriber)\n"
    << "  --mqtt-user <str>     MQTT username\n"
    << "  --mqtt-pass <str>     MQTT password\n"
    << "  --mqtt-qos <0|1|2>    MQTT QoS level (default: 0)\n\n"
    << "Storage ([storage]):\n"
    << "  --output <file>       CSV output path (default: auto)\n"
    << "  --buf <int>           Ring buffer capacity (default: 4096)\n"
    << "  --flush-ms <int>      Flush interval ms (default: 500)\n\n"
    << "Logging ([logging]):\n"
    << "  --log <path>          Log file (stderr|stdout|default|<path>)\n"
    << "  --no-log-stderr       Don't duplicate log to stderr\n"
    << "  --log-level <level>   DEBUG|INFO|WARN|ERROR (default: INFO)\n\n"
    << "Misc:\n"
    << "  --help                Show this message\n";
};

#endif

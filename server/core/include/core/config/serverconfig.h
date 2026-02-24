#ifndef __SERVERCONFIG_H__
#define __SERVERCONFIG_H__

#include <core/common/systypes.h>
#include <core/protocol/transportprotocol.h>
#include <string>
#include <cstddef>
#include <iostream>

namespace server
{
    namespace core
    {
        namespace types
        {
            struct config_t
            {
                core::types::transport_type_t transport = core::types::transport_type_t::TRANSPORT_WEBSOCKET;
                std::string host = "localhost";
                net_port_t port = 8080;
                /*std::*/size_t buffer_capacity = 4096;   ///< Максимальный размер кольцевого буфера (пакеты) Max ringbuffer capacity
                /*std::*/size_t flush_interval_ms = 500;  ///< Период сброса буфера на диск (мс) Flush buffer interval in ms
            };
        }
        static void print_usage(const char* prog)
        {
            std::cout
            << "Usage: " << prog << " [OPTIONS]\n"
            << "\n"
            << "Transport:\n"
            << "  --ws              WebSocket server mode (default)\n"
            << "  --mqtt            MQTT subscriber mode\n"
            << "\n"
            << "Connection:\n"
            << "  --host <str>      MQTT broker host (default: localhost)\n"
            << "  --port <int>      Port (WS listen / MQTT connect) (default: 8080)\n"
            << "  --topic <str>     MQTT topic (default: sensors/accel)\n"
            << "\n"
            << "Storage:\n"
            << "  --output <file>   Output CSV file path (default: data.csv)\n"
            << "  --buf <int>       Ring buffer capacity in packets (default: 4096)\n"
            << "  --flush-ms <int>  Buffer flush interval ms (default: 500)\n"
            << "\n"
            << "Misc:\n"
            << "  --help            Show this message\n";
        }
        static server::core::types::config_t parse_args(int argc, char* argv[])
        {
            server::core::types::config_t conf;

            for (int i = 1; i < argc; ++i)
            {
                std::string a = argv[i];

                if (a == "--help" || a == "-h")
                {
                    server::core::print_usage(argv[0]);
                    std::exit(0);
                }
                else if (a == "--ws")
                {
                    //cfg.transport = subscriber::TransportType::WebSocket;
                }
                else if (a == "--mqtt")
                {
                    //cfg.transport = subscriber::TransportType::MQTT;
                }
                else if (a == "--host" && i + 1 < argc)
                {
                    //cfg.host = argv[++i];
                }
                else if (a == "--port" && i + 1 < argc)
                {
                    //cfg.port = static_cast<uint16_t>(std::atoi(argv[++i]));
                }
                else if (a == "--topic" && i + 1 < argc)
                {
                    //cfg.mqtt_topic = argv[++i];
                }
                else if (a == "--output" && i + 1 < argc)
                {
                    //cfg.output_file = argv[++i];
                }
                else if (a == "--buf" && i + 1 < argc)
                {
                    //cfg.buffer_capacity = static_cast<std::size_t>(std::atoi(argv[++i]));
                }
                else if (a == "--flush-ms" && i + 1 < argc)
                {
                    //cfg.flush_interval_ms = static_cast<std::size_t>(std::atoi(argv[++i]));
                }
                else
                {
                    std::cerr << "[WARN] Unknown argument: " << a << "\n";
                }
            }

            return conf;
        };
    }
}

#endif

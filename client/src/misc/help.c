#include <misc/help.h>
#include <stdio.h>

void
print_usage(const char* prog)
{
    printf(
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Connection:\n"
        "  --protocol <str>     Protocol                (default: mqtt)\n"
        "  --host <str>         Broker address          (default: localhost)\n"
        "  --port <uint16>      Broker port             (default: 1883)\n"
        "  --topic <str>        MQTT topic              (default: sensors/accel)\n"
        "  --qos <0|1|2>        QoS level               (default: 0)\n"
        "  --id <str>           MQTT client ID          (default: auto)\n"
        "  --user <str>         Username\n"
        "  --pass <str>         Password\n"
        "  --keepalive <sec>    PINGREQ interval        (default: 60)\n"
        "  --no-clean           Disable clean session\n"
        "\n"
        "TLS:\n"
        "  --tls                Enable TLS (default port becomes 8883)\n"
        "  --cafile <path>      CA certificate (PEM)\n"
        "  --capath <dir>       Directory with CA certificates\n"
        "  --cert <path>        Client certificate (mutual TLS)\n"
        "  --key <path>         Client private key\n"
        "  --tls-insecure       Skip hostname verification (debug only)\n"
        "\n"
        "Last Will:\n"
        "  --will-topic <str>   LWT topic\n"
        "  --will-payload <str> LWT message payload\n"
        "  --will-qos <0|1|2>   LWT QoS\n"
        "  --will-retain        Set retain flag on LWT\n"
        "\n"
        "Timing:\n"
        "  --duration <sec>     Run duration in seconds (default: 10)\n"
        "  --rate <hz>          Packets per second      (default: 10)\n"
        "  --timeout <ms>       Connect timeout ms      (default: 10000)\n"
        "\n"
        "Sensor (mock parameters):\n"
        "  --noise <float>      Noise amplitude m/s2    (default: 0.05)\n"
        "  --gravity <float>    Gravity Z m/s2          (default: 9.81)\n"
        "\n"
        "Misc:\n"
        "  --retain             Set retain flag on every data packet\n"
        "  --verbose            Print each packet\n"
        "  --help               Show this message\n"
        "\n"
        "Examples:\n"
        "  %s\n"
        "  %s --host broker.local --port 1883 --topic factory/sensors --qos 1\n"
        "  %s --host broker.local --user alice --pass secret --duration 60\n"
        "  %s --tls --cafile /path/to/ssl/ca.pem --cert client.crt --key client.key\n",
        prog, prog, prog, prog, prog
    );
}

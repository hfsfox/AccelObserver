#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <string>
#include <cstring>

#include <misc/help.hpp>

#ifdef ENABLE_CONFPARSER
extern "C"
{
    #include <confparser.h>
}
#endif

#ifdef HAVE_WEBSOCKET
    //#include <transport/websocket/ws_subscriber.hpp>
#endif

#ifdef HAVE_MQTT
    //#include <transport/mqtt/mqtt_subscriber.hpp>
#endif

#ifdef HAVE_WEBUI
    //#include <web/web_interface.hpp>
#endif

// atomic running state for begin and end main event loop
static std::atomic<bool> g_running{true};
static void on_signal(int) { g_running = false; }

// wall-clock time in milliseconds (steady_clock for monotonic latency calc)
static uint64_t now_ms()
{
    //using namespace std::chrono;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()
            );
}

int main(int argc, char* argv[])
{
}

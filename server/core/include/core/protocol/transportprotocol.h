#ifndef __PROTOCOLS_H__
#define __PROTOCOLS_H__

/*
enum transport_type_t
{
    TRANSPORT_WEBSOCKET,
    TRANSPORT_MQTT
    TRANSPORT_UNKNOWN
};
*/

namespace server
{
    namespace core
    {
        namespace types
        {
            enum transport_type_t
            {
                TRANSPORT_WEBSOCKET,
                TRANSPORT_MQTT,
                TRANSPORT_UNKNOWN
            };
        }
    }
}

#endif

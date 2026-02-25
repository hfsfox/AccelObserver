#ifndef __WEBSOCKET_H__
#define __WEBSOCKET_H__

#include <transport/itransport.h>
#include <core/core.h>

namespace server
{
    namespace transport
    {
        class WebSocketTransport: public ITransport
        {
            public:
                WebSocketTransport();
                ~WebSocketTransport() override;
                // Non-copyable
                WebSocketTransport(const WebSocketTransport&) = delete;
                WebSocketTransport& operator=(const WebSocketTransport&) = delete;

                bool        connect(const std::string& host, uint16_t port) override;
                void        set_callback(
                    server::transport::types::message_callback_t cb) override;
                void        run() override;
                void        stop() override;
                const char* name() const override { return "WebSocket"; }
            private:
        };
    }
}

#endif

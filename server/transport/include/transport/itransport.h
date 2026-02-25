#ifndef __ITRANSPORT_H__
#define __ITRANSPORT_H__

#include <functional>
#include <string>
#include <cstdint>

namespace server
{
    namespace transport
    {
        namespace types
        {
            /// Callback, вызываемый при получении нового сообщения.
            using message_callback_t =
            std::function<void(const std::string& payload)>;
        }


        class ITransport
        {
            public:
                virtual ~ITransport() = default;
            public:
                /// Подключиться к брокеру/серверу (MQTT) или начать прослушивать порт (WebSocket).
                /// host игнорируется для WsSubscriber (сервер слушает 0.0.0.0:port).
                virtual bool connect(const std::string& host, uint16_t port) = 0;

                /// Установить callback для входящих сообщений.
                virtual void set_callback(server::transport::types::message_callback_t cb) = 0;

                /// Запустить цикл приёма сообщений (блокирующий).
                virtual void run() = 0;

                /// Остановить цикл приёма (потокобезопасно, вызывается из другого потока).
                virtual void stop() = 0;

                /// Имя транспорта для логов.
                virtual const char* name() const = 0;
        };
    }
}

#endif

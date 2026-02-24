#ifndef __IFORMATTER_H__
#define __IFORMATTER_H__

#include <vector>
#include <string>

namespace server
{
    namespace format
    {
        namespace types
        {
            struct data_packet_t
            {
                uint64_t timestamp_ms;  ///< Unix-время в миллисекундах (UTC)
                uint32_t sequence_id;   ///< Монотонный порядковый номер пакета
                double   acc_x;         ///< Ускорение по оси X, м/с²
                double   acc_y;         ///< Ускорение по оси Y, м/с²
                double   acc_z;         ///< Ускорение по оси Z, м/с²
            };
        }
        class IFormatter
        {
            public:
                virtual ~IFormatter() = default;
                /// Создать файл и записать строку заголовка (вызывается один раз при открытии).
                /// Если файл уже существует и содержит корректный заголовок — пропустить запись.
                /// Возвращает true при успехе.
                virtual bool write_header(const std::string& filepath) = 0;
                /// Дозаписать пакет данных в конец файла.
                /// Вызывается потоком записи; может быть вызвано несколько раз.
                virtual bool write_packets(const std::string& filepath,
                                           const std::vector<server::format::types::data_packet_t>& packets) = 0;
        };
    }
}

#endif

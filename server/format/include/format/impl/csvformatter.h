#ifndef __CSVFORMATTER_H__
#define __CSVFORMATTER_H__

#include <format/iformatter.h>

#include <unordered_set>
#include <string>

namespace server
{
    namespace format
    {
        class CsvFormatter : public IFormatter
        {
            public:
                /// Заголовок CSV-файла — строго задан
                static const char* HEADER;

                bool write_header(const std::string& filepath) override;

                bool write_packets(const std::string& filepath,
                       const std::vector<server::format::types::data_packet_t>& packets) override;
            private:
                // Множество файлов, в которых заголовок уже записан
                std::unordered_set<std::string> _initialized;
        };
    }
}



#endif

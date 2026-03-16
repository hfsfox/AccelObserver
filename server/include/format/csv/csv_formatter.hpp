#pragma once
#include <format/iformatter.hpp>
#include <unordered_set>
#include <string>

namespace server
{
    class CsvFormatter : public IFormatter
    {
        public:
            // fixed header format
            static const char* HEADER;

            bool write_header(const std::string& filepath) override;

            bool write_packets(const std::string& filepath,
                            const std::vector<DataPacket>& packets) override;

        private:
            // multiple files where headers exist
            std::unordered_set<std::string> initialized_;
    };

}

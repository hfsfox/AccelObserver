#pragma once
#include <format/iformatter.hpp>
#include <unordered_set>
#include <string>

namespace server
{
    class CsvFormatter : public IFormatter
    {
    public:
        // separator: column delimiter written between fields (default ";").
        // The HEADER row is generated using the same separator so that
        // consumers can parse both header and data rows with a single delimiter.
        explicit CsvFormatter(const std::string& separator = ";");

        bool write_header(const std::string& filepath) override;

        bool write_packets(const std::string& filepath,
                           const std::vector<DataPacket>& packets) override;

    private:
        std::string separator_;

        // Builds the header string for the configured separator.
        std::string make_header() const;

        // Tracks files that already have a valid header so it is not rewritten.
        std::unordered_set<std::string> initialized_;
    };
}

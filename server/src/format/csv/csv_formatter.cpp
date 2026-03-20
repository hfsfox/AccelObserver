// =============================================================================
// format/csv_formatter.cpp
// =============================================================================
#include <format/csv/csv_formatter.hpp>
#include <fstream>
#include <iomanip>

namespace server
{

    const char* CsvFormatter::HEADER = "timestamp;sequence_id;acc_x;acc_y;acc_z";
    const char* CsvSeparator = ";";

    // ---------------------------------------------------------------------------
    bool CsvFormatter::write_header(const std::string& filepath)
    {
        //if exist and first row match header - not overvrite
        {
            std::ifstream check(filepath);
            if (check.is_open())
            {
                std::string first_line;
                if (std::getline(check, first_line) && first_line == HEADER)
                {
                    initialized_.insert(filepath);
                    return true;
                }
            }
        }

        // create/overvrite file only if empty or not exist
        std::ofstream file(filepath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) return false;

        file << HEADER << "\n";
        if (!file.good()) return false;

        initialized_.insert(filepath);
        return true;
    }

    bool CsvFormatter::write_packets(const std::string& filepath,
                                    const std::vector<DataPacket>& packets)
    {
        if (packets.empty()) return true;

        // guarantee write header in first iteration
        if (initialized_.find(filepath) == initialized_.end())
        {
            if (!write_header(filepath)) return false;
        }

        std::ofstream file(filepath, std::ios::out | std::ios::app);
        if (!file.is_open()) return false;


        // fixed precision for double - 6 digits after comma
        file << std::fixed << std::setprecision(6);

        for (const auto& pkt : packets) {
            file << pkt.timestamp_ms << CsvSeparator//";"
                << pkt.sequence_id  << CsvSeparator//";"
                << pkt.acc_x        << CsvSeparator//";"
                << pkt.acc_y        << CsvSeparator//";"
                << pkt.acc_z        << "\n";
            if (!file.good()) return false;
        }

        return true;
    }

}

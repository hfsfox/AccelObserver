#include <format/impl/csvformatter.h>
#include <fstream>
#include <iomanip>

namespace server
{
    namespace format
    {
        const char* CsvFormatter::HEADER = "timestamp,sequence_id,acc_x,acc_y,acc_z";
        bool CsvFormatter::write_header(const std::string& filepath)
        {
            //if file exist and first line math with header - not overwrite
            {
                std::ifstream check(filepath);
                if (check.is_open()) {
                    std::string first_line;
                    if (std::getline(check, first_line) && first_line == HEADER)
                    {
                        _initialized.insert(filepath);
                        return true;
                    }
                }
            }

            //create/rewrite file only if empty or not exist
            std::ofstream file(filepath, std::ios::out | std::ios::trunc);
            if (!file.is_open()) return false;

            file << HEADER << "\n";
            if (!file.good()) return false;

            _initialized.insert(filepath);
            return true;
        }

        bool CsvFormatter::write_packets(const std::string& filepath,
                                         const std::vector<server::format::types::data_packet_t>& packets)
        {
            if (packets.empty()) return true;
            //guarantee header in first request
            if (_initialized.find(filepath) == _initialized.end())
            {
                if (!write_header(filepath)) return false;
            }

            std::ofstream file(filepath, std::ios::out | std::ios::app);
            if (!file.is_open()) return false;
            //fixed precision for double values - 6 digits after comma
            file << std::fixed << std::setprecision(6);

            for (const auto& pkt : packets)
            {
                file << pkt.timestamp_ms << ","
                << pkt.sequence_id  << ","
                << pkt.acc_x        << ","
                << pkt.acc_y        << ","
                << pkt.acc_z        << "\n";
                if (!file.good()) return false;
            }

            return true;
        }

    }
}

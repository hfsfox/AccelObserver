// =============================================================================
// format/csv_formatter.cpp
// =============================================================================
#include <format/csv/csv_formatter.hpp>
#include <fstream>
#include <iomanip>

namespace server
{

CsvFormatter::CsvFormatter(const std::string& separator)
    : separator_(separator)
{}

// Builds the header row using the configured separator.
std::string CsvFormatter::make_header() const
{
    const std::string s = separator_;
    return "timestamp" + s + "sequence_id" + s +
           "acc_x"     + s + "acc_y"       + s + "acc_z";
}

// ---------------------------------------------------------------------------
bool CsvFormatter::write_header(const std::string& filepath)
{
    const std::string header = make_header();

    // If the file already starts with a matching header do not overwrite it.
    {
        std::ifstream check(filepath);
        if (check.is_open())
        {
            std::string first_line;
            if (std::getline(check, first_line) && first_line == header)
            {
                initialized_.insert(filepath);
                return true;
            }
        }
    }

    // Create or truncate the file and write the header.
    std::ofstream file(filepath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;

    file << header << "\n";
    if (!file.good()) return false;

    initialized_.insert(filepath);
    return true;
}

bool CsvFormatter::write_packets(const std::string& filepath,
                                 const std::vector<DataPacket>& packets)
{
    if (packets.empty()) return true;

    // Guarantee the header is written on the first call for this filepath.
    if (initialized_.find(filepath) == initialized_.end())
    {
        if (!write_header(filepath)) return false;
    }

    std::ofstream file(filepath, std::ios::out | std::ios::app);
    if (!file.is_open()) return false;

    // Fixed precision for floating-point values (6 digits after decimal point).
    file << std::fixed << std::setprecision(6);

    const std::string& s = separator_;
    for (const auto& pkt : packets)
    {
        file << pkt.timestamp_ms << s
             << pkt.sequence_id  << s
             << pkt.acc_x        << s
             << pkt.acc_y        << s
             << pkt.acc_z        << "\n";
        if (!file.good()) return false;
    }

    return true;
}

} // namespace server

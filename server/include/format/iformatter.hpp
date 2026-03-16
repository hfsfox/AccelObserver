#ifndef __IFORMATTER_H__
#define __IFORMATTER_H__

#pragma once
// abstract interface of storage fromatter module
// interface adapter for csv implementation
// allow udate or change implementation of storing format
// withput compat break or core related changes
#include <core/servertypes.hpp>
#include <vector>
#include <string>

namespace server
{

    class IFormatter
    {
        public:
            virtual ~IFormatter() = default;
            // create file > write header (once at open)
            // if file already exist and already contain valid header - skip header part
            // return true if success
            virtual bool write_header(const std::string& filepath) = 0;
            // write packets to file tail
            // call from write thread and may be called multiple times
            virtual bool write_packets(const std::string& filepath,
                                    const std::vector<DataPacket>& packets) = 0;
    };

}
#endif

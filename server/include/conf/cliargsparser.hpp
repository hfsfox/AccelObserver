#ifndef __CLIARGSPARSER_H__
#define __CLIARGSPARSER_H__

#include <core/servertypes.hpp>

server::Config
parse_args(int argc, char* argv[], server::Config cfg);

void
dump_config(const server::Config& cfg);

#endif

#ifndef __CONFLOADER_H__
#define __CONFLOADER_H__

#ifdef HAVE_CONFPARSER
    #include <confparser.h>

    void
    apply_conf(const conf_result_t* conf, server::Config& cfg);
#endif

#endif

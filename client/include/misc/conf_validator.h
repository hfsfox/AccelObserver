#ifndef __CONF_VALIDATOR_H__
#define __CONF_VALIDATOR_H__

#include <stdint.h>
#include <stdio.h>
#include <core/clienttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// config struct validation
int validate_config(const app_config_t* cfg);

#ifdef __cplusplus
}
#endif

#endif

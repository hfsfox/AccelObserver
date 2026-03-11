#ifndef __MQTT_CLIENT_H__
#define __MQTT_CLIENT_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <transport/mqtt/mqtt_types.h>

#ifdef __cplusplus
extern "C" {
#endif

//
void
mqtt_lib_init(void);

//
void
mqtt_lib_cleanup(void);

//
mqtt_client_t*
mqtt_connect(const mqtt_config_t* cfg, mqtt_error_code_t* err_out);

//
mqtt_error_code_t
mqtt_publish(mqtt_client_t* client,
                        const char* topic,
                        const char* payload,
                        size_t      len,
                        int         qos,
                        bool        retain);
                        
//
void mqtt_disconnect(mqtt_client_t* client);

//
void mqtt_destroy(mqtt_client_t* client);

//
bool
mqtt_is_connected(const mqtt_client_t* client);

//
void
mqtt_get_stats(const mqtt_client_t* client, mqtt_stats_t* out);

//
const char*
mqtt_error_str(mqtt_error_code_t err);

#ifdef __cplusplus
}
#endif

#endif

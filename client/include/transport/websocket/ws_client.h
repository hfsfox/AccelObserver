#ifndef __WS_CLIENT_H__
#define __WS_CLIENT_H__

#include <stddef.h>
#include <transport/websocket/ws_types.h>
#include <network/net_platform.h>

// create and connect to websocket server
ws_client_t*
ws_connect(const ws_config_t* cfg, ws_error_code_t* err_out);

// send text frame (opcode=0x1, masked)
// data must be valid UTF-8 (guaranted for JSON)
ws_error_code_t
ws_send_text(ws_client_t* ws, const char* data, size_t len);

// correctly close connection
void
ws_close(ws_client_t* ws);

//
void
ws_destroy(ws_client_t* ws);

//
const char*
ws_error_str(ws_error_code_t err);

//
socket_t ws_socket(const ws_client_t* ws);

#endif

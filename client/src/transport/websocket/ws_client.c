#include <transport/websocket/ws_types.h>
#include <transport/websocket/ws_client.h>
#include <network/net_platform.h>
#include <crypto/sha1.h>
#include <crypto/base64.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>

/* WebSocket GUID (RFC 6455 par.4.2.2) */
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* max len of one HTTP-header string */
#define HTTP_LINE_MAX  2048

/* buffer size for receive in main loop (not used — client is not reading) */
#define RECV_BUF_SIZE  4096

struct ws_client_t
{
    socket_t sock;
    uint32_t rng_state;
};

// Xorshift32 PRNG for mask gen (RFC 6455 §5.3 must be random)

static uint32_t
prng_next(uint32_t* state)
{
    uint32_t x = *state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x <<  5u;
    *state = x;
    return x;
}

static uint32_t
prng_seed(void)
{
    uint32_t s = (uint32_t)time(NULL);
    #ifdef _WIN32
    s ^= (uint32_t)GetCurrentProcessId() * 2654435761u;
    #else
    s ^= (uint32_t)getpid() * 2654435761u;
    #endif
    for (int i = 0; i < 16; ++i) {
        s ^= s << 13u; s ^= s >> 17u; s ^= s << 5u;
    }
    return (s == 0) ? 0xDEADBEEFu : s;
}

bool
istr_contains(const char* haystack, const char* needle)
{
    if (!haystack || !needle) return false;
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; ++i) {
        bool match = true;
        for (size_t j = 0; j < nlen; ++j) {
            char h = (char)tolower((unsigned char)haystack[i + j]);
            char n = (char)tolower((unsigned char)needle[j]);
            if (h != n) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

bool
extract_header_value(const char* line,
                                 const char* name,
                                 char* out,
                                 size_t out_size)
{
    if (!istr_contains(line, name)) return false;
    const char* colon = strchr(line, ':');
    if (!colon) return false;
    ++colon;
    while (*colon == ' ' || *colon == '\t') ++colon;
    size_t vlen = strlen(colon);
    while (vlen > 0 && (colon[vlen-1] == ' ' || colon[vlen-1] == '\t' ||
        colon[vlen-1] == '\r' || colon[vlen-1] == '\n'))
        --vlen;
    if (vlen >= out_size) return false;
    memcpy(out, colon, vlen);
    out[vlen] = '\0';
    return true;
}

ws_error_code_t
do_handshake(ws_client_t* ws,
                            const char* host,
                            uint16_t    port,
                            const char* path)
{
    /* random 16 bytes nonce -> Base64 */
    uint8_t nonce_raw[16];
    for (int i = 0; i < 16; ++i)
        nonce_raw[i] = (uint8_t)(prng_next(&ws->rng_state) & 0xFF);

    char nonce_b64[32]; /* ceil(16/3)*4 + 1 = 25 */
    base64_encode(nonce_raw, 16, nonce_b64);

    /* HTTP Upgrade request */
    char request[1024];
    int reqlen = snprintf(request, sizeof(request),
                          "GET %s HTTP/1.1\r\n"
                          "Host: %s:%u\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Key: %s\r\n"
                          "Sec-WebSocket-Version: 13\r\n"
                          "\r\n",
                          path, host, (unsigned)port, nonce_b64);

    if (reqlen <= 0 || (size_t)reqlen >= sizeof(request))
        return WS_ERR_PARAM;

    if (!net_send_exact(ws->sock, request, (size_t)reqlen))
        return WS_ERR_NET;

    /* calculate excepted Sec-WebSocket-Accept */
    char accept_input[128];
    snprintf(accept_input, sizeof(accept_input), "%s%s", nonce_b64, WS_GUID);

    uint8_t digest[20];
    sha1_compute((const uint8_t*)accept_input, strlen(accept_input), digest);

    char expected_accept[32];
    base64_encode(digest, 20, expected_accept);

    /* read HTTP-headers reply before empty string */
    char line[HTTP_LINE_MAX];
    bool got_101           = false;
    bool got_upgrade       = false;
    bool got_connection    = false;
    char server_accept[64] = {0};

    /* first status string */
    if (net_recv_line(ws->sock, line, sizeof(line)) < 0)
        return WS_ERR_NET;

    if (!istr_contains(line, "101")) {
        fprintf(stderr, "[WS] Handshake: unexpected status: %s\n", line);
        return WS_ERR_HANDSHAKE;
    }
    got_101 = true;
    (void)got_101;

    /* headers */
    while (true) {
        int n = net_recv_line(ws->sock, line, sizeof(line));
        if (n < 0)  return WS_ERR_NET;
        if (n == 0) break; /* empty string — header end */

            if (istr_contains(line, "upgrade:") && istr_contains(line, "websocket"))
                got_upgrade = true;

        if (istr_contains(line, "connection:") && istr_contains(line, "upgrade"))
            got_connection = true;

        if (istr_contains(line, "sec-websocket-accept:"))
            extract_header_value(line, "sec-websocket-accept:",
                                 server_accept, sizeof(server_accept));
    }

    if (!got_upgrade) {
        fprintf(stderr, "[WS] Handshake: missing 'Upgrade: websocket'\n");
        return WS_ERR_HANDSHAKE;
    }
    if (!got_connection) {
        fprintf(stderr, "[WS] Handshake: missing 'Connection: Upgrade'\n");
        return WS_ERR_HANDSHAKE;
    }
    if (server_accept[0] == '\0') {
        fprintf(stderr, "[WS] Handshake: missing Sec-WebSocket-Accept\n");
        return WS_ERR_HANDSHAKE;
    }

    /* check accept-key (RFC 6455 §4.1 p.4) */
    if (strcmp(server_accept, expected_accept) != 0) {
        fprintf(stderr, "[WS] Handshake: accept-key mismatch\n"
        "  Got      : %s\n"
        "  Expected : %s\n",
        server_accept, expected_accept);
        return WS_ERR_HANDSHAKE;
    }

    return WS_OK;
}

ws_client_t*
ws_connect(const ws_config_t* cfg, ws_error_code_t* err_out)
{
    if (!cfg || !cfg->host) {
        if (err_out) *err_out = WS_ERR_PARAM;
        return NULL;
    }

    ws_client_t* ws = (ws_client_t*)malloc(sizeof(ws_client_t));
    if (!ws)
    {
        if (err_out) *err_out = WS_ERR_ALLOC;
        return NULL;
    }
    ws->rng_state = prng_seed();
    ws->sock      = INVALID_SOCK;

    /* TCP connect */
    ws->sock = net_connect_tcp(cfg->host, cfg->port);
    if (!SOCK_VALID(ws->sock))
    {
        fprintf(stderr, "[WS] connect_tcp failed: %s\n", net_last_error());
        free(ws);
        if (err_out) *err_out = WS_ERR_NET;
        return NULL;
    }

    /* handshake timeout */
    uint32_t hs_timeout = cfg->handshake_timeout_ms ? cfg->handshake_timeout_ms : 5000u;
    net_set_recv_timeout(ws->sock, hs_timeout);

    /* WebSocket handshake */
    const char* path = (cfg->path && cfg->path[0]) ? cfg->path : "/";
    ws_error_code_t err = do_handshake(ws, cfg->host, cfg->port, path);

    if (err != WS_OK)
    {
        SOCK_CLOSE(ws->sock);
        free(ws);
        if (err_out) *err_out = err;
        return NULL;
    }

    net_set_recv_timeout(ws->sock, 0);

    if (err_out) *err_out = WS_OK;
    return ws;
}

ws_error_code_t
ws_send_text(ws_client_t* ws, const char* data, size_t len)
{
    if (!ws || !SOCK_VALID(ws->sock)) return WS_ERR_PARAM;
    if (!data) return WS_ERR_PARAM;

    uint32_t mask32 = prng_next(&ws->rng_state);
    uint8_t  mask[4];
    mask[0] = (uint8_t)(mask32 >> 24u);
    mask[1] = (uint8_t)(mask32 >> 16u);
    mask[2] = (uint8_t)(mask32 >>  8u);
    mask[3] = (uint8_t)(mask32       );

    uint8_t  hdr[14];
    size_t   hdr_len = 0;

    hdr[hdr_len++] = 0x81u; /* FIN=1, RSV=000, opcode=0x1 (text) */

    if (len < 126u) {
        hdr[hdr_len++] = 0x80u | (uint8_t)len;
    } else if (len < 65536u) {
        hdr[hdr_len++] = 0x80u | 126u;
        hdr[hdr_len++] = (uint8_t)(len >> 8u);
        hdr[hdr_len++] = (uint8_t)(len      );
    } else {
        hdr[hdr_len++] = 0x80u | 127u;
        for (int i = 7; i >= 0; --i)
            hdr[hdr_len++] = (uint8_t)(len >> ((unsigned)i * 8u));
    }

    hdr[hdr_len++] = mask[0];
    hdr[hdr_len++] = mask[1];
    hdr[hdr_len++] = mask[2];
    hdr[hdr_len++] = mask[3];

    if (!net_send_exact(ws->sock, hdr, hdr_len))
        return WS_ERR_NET;

    uint8_t  chunk[4096];
    size_t   sent = 0;
    while (sent < len) {
        size_t   chunk_len = len - sent;
        if (chunk_len > sizeof(chunk)) chunk_len = sizeof(chunk);
        for (size_t i = 0; i < chunk_len; ++i)
            chunk[i] = (uint8_t)((unsigned char)data[sent + i]) ^ mask[(sent + i) & 3u];
        if (!net_send_exact(ws->sock, chunk, chunk_len))
            return WS_ERR_NET;
        sent += chunk_len;
    }
    return WS_OK;
}

void
ws_close(ws_client_t* ws) {
    if (!ws) return;

    if (SOCK_VALID(ws->sock))
    {
        uint32_t mask32 = prng_next(&ws->rng_state);
        uint8_t  mask[4] = {
            (uint8_t)(mask32 >> 24u), (uint8_t)(mask32 >> 16u),
            (uint8_t)(mask32 >>  8u), (uint8_t)(mask32)
        };
        /* Status code 1000 = 0x03E8 */
        uint8_t payload[2] = { 0x03u, 0xE8u };
        uint8_t frame[8] = {
            0x88u,               /* FIN=1, opcode=Close */
            0x80u | 2u,          /* MASK=1, length=2    */
            mask[0], mask[1], mask[2], mask[3],
            (uint8_t)(payload[0] ^ mask[0]),
            (uint8_t)(payload[1] ^ mask[1])
        };
        net_send_exact(ws->sock, frame, sizeof(frame));

        net_set_recv_timeout(ws->sock, 2000);
        uint8_t echo_buf[16];
        net_recv_exact(ws->sock, echo_buf, 2);

        SOCK_CLOSE(ws->sock);
        ws->sock = INVALID_SOCK;
    }
    free(ws);
}

void
ws_destroy(ws_client_t* ws)
{
    if (!ws) return;
    if (SOCK_VALID(ws->sock))
    {
        SOCK_CLOSE(ws->sock);
        ws->sock = INVALID_SOCK;
    }
    free(ws);
}

const char* ws_error_str(ws_error_code_t err)
{
    switch (err) {
        case WS_OK:            return "OK";
        case WS_ERR_NET:       return "Network error";
        case WS_ERR_HANDSHAKE: return "WebSocket handshake failed";
        case WS_ERR_FRAME:     return "Frame error";
        case WS_ERR_CLOSED:    return "Connection closed by server";
        case WS_ERR_ALLOC:     return "Memory allocation failed";
        case WS_ERR_PARAM:     return "Invalid parameters";
        default:               return "Unknown error";
    }
}

socket_t
ws_socket(const ws_client_t* ws)
{
    return ws ? ws->sock : INVALID_SOCK;
}

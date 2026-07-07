#ifndef __HTTP_CLIENT_H
#define __HTTP_CLIENT_H

#include <stdint.h>

#define HTTP_CLIENT_OK                 0
#define HTTP_CLIENT_ERR_PARAM         -1
#define HTTP_CLIENT_ERR_CONNECT       -2
#define HTTP_CLIENT_ERR_SEND          -3
#define HTTP_CLIENT_ERR_RECV          -4
#define HTTP_CLIENT_ERR_RESPONSE_FULL -5

int http_get(const uint8_t server_ip[4],
             uint16_t server_port,
             const char *host,
             const char *path,
             char *response,
             uint16_t response_size);

int http_post(const uint8_t server_ip[4],
              uint16_t server_port,
              const char *host,
              const char *path,
              const char *content_type,
              const char *body,
              char *response,
              uint16_t response_size);

const char *http_get_body(const char *http_response);

#endif


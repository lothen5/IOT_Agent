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

/*
 * 带接收超时参数的 POST。
 *
 * 普通 Agent 请求继续调用 http_post()。
 * 周期遥测可以使用较短超时，避免服务端异常时
 * 长时间占用网络任务。
 */
int http_post_timeout(const uint8_t server_ip[4],
                      uint16_t server_port,
                      const char *host,
                      const char *path,
                      const char *content_type,
                      const char *body,
                      char *response,
                      uint16_t response_size,
                      uint32_t recv_timeout_ms);

const char *http_get_body(const char *http_response);

#endif

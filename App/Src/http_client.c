#include "http_client.h"

#include "w5500.h"

#include <stdio.h>
#include <string.h>

#define HTTP_REQUEST_BUF_SIZE      512
#define HTTP_RECV_TEMP_SIZE        256
#define HTTP_DEFAULT_RECV_TIMEOUT_MS 3000U

static uint16_t http_local_port = 50000;

static uint16_t http_get_next_local_port(void)
{
    http_local_port++;

    if (http_local_port > 60000)
    {
        http_local_port = 50000;
    }

    return http_local_port;
}

int http_get(const uint8_t server_ip[4],
             uint16_t server_port,
             const char *host,
             const char *path,
             char *response,
             uint16_t response_size)
{
    char request[HTTP_REQUEST_BUF_SIZE];
    uint8_t recv_temp[HTTP_RECV_TEMP_SIZE];
    int ret;
    int recv_len;
    uint16_t total_len = 0;
    uint16_t local_port;

    if (server_ip == NULL || host == NULL || path == NULL ||
        response == NULL || response_size == 0)
    {
        return HTTP_CLIENT_ERR_PARAM;
    }

    response[0] = '\0';

    ret = snprintf(request,
                   sizeof(request),
                   "GET %s HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   path,
                   host);

    if (ret <= 0 || ret >= (int)sizeof(request))
    {
        return HTTP_CLIENT_ERR_PARAM;
    }

    local_port = http_get_next_local_port();

    printf("HTTP GET connect %d.%d.%d.%d:%d\r\n",
           server_ip[0], server_ip[1], server_ip[2], server_ip[3], server_port);

    ret = W5500_Socket0_ConnectTCP(server_ip, server_port, local_port);

    if (ret != 0)
    {
        printf("HTTP connect failed, ret=%d\r\n", ret);
        W5500_Socket0_Close();
        return HTTP_CLIENT_ERR_CONNECT;
    }

    ret = W5500_Socket0_Send((const uint8_t *)request, (uint16_t)strlen(request));

    if (ret < 0)
    {
        printf("HTTP send failed, ret=%d\r\n", ret);
        W5500_Socket0_Close();
        return HTTP_CLIENT_ERR_SEND;
    }

    printf("HTTP request sent, len=%d\r\n", ret);

    while (1)
    {
        recv_len = W5500_Socket0_Recv(recv_temp,
                                      sizeof(recv_temp),
                                      3000);

        if (recv_len > 0)
        {
            if ((total_len + recv_len) >= (response_size - 1))
            {
                uint16_t copy_len = (response_size - 1) - total_len;

                if (copy_len > 0)
                {
                    memcpy(&response[total_len], recv_temp, copy_len);
                    total_len += copy_len;
                    // 添加终止符，确保响应字符串以 NULL 结尾
                    response[total_len] = '\0';
                }

                W5500_Socket0_Close();
                return HTTP_CLIENT_ERR_RESPONSE_FULL;
            }

            memcpy(&response[total_len], recv_temp, recv_len);
            total_len += recv_len;
            response[total_len] = '\0';
        }
        else if (recv_len == 0)
        {
            break;
        }
        else
        {
            W5500_Socket0_Close();
            return HTTP_CLIENT_ERR_RECV;
        }
    }

    W5500_Socket0_Close();

    printf("HTTP GET done, response len=%d\r\n", total_len);

    return total_len;
}

int http_post_timeout(const uint8_t server_ip[4],
                      uint16_t server_port,
                      const char *host,
                      const char *path,
                      const char *content_type,
                      const char *body,
                      char *response,
                      uint16_t response_size,
                      uint32_t recv_timeout_ms)
{
    char header[HTTP_REQUEST_BUF_SIZE];
    uint8_t recv_temp[HTTP_RECV_TEMP_SIZE];

    int ret;
    int recv_len;

    uint16_t total_len = 0;
    uint16_t local_port;
    uint32_t body_len;

    if (server_ip == NULL ||
        host == NULL ||
        path == NULL ||
        content_type == NULL ||
        body == NULL ||
        response == NULL ||
        response_size == 0U ||
        recv_timeout_ms == 0U)
    {
        return HTTP_CLIENT_ERR_PARAM;
    }

    response[0] = '\0';

    body_len = (uint32_t)strlen(body);

    ret = snprintf(
        header,
        sizeof(header),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "\r\n",
        path,
        host,
        content_type,
        (unsigned long)body_len
    );

    if (ret <= 0 ||
        ret >= (int)sizeof(header))
    {
        return HTTP_CLIENT_ERR_PARAM;
    }

    local_port = http_get_next_local_port();

    printf("HTTP POST connect %d.%d.%d.%d:%d\r\n",
           server_ip[0],
           server_ip[1],
           server_ip[2],
           server_ip[3],
           server_port);

    ret = W5500_Socket0_ConnectTCP(
        server_ip,
        server_port,
        local_port
    );

    if (ret != 0)
    {
        printf("HTTP POST connect failed, ret=%d\r\n",
               ret);

        W5500_Socket0_Close();

        return HTTP_CLIENT_ERR_CONNECT;
    }

    ret = W5500_Socket0_Send(
        (const uint8_t *)header,
        (uint16_t)strlen(header)
    );

    if (ret < 0)
    {
        printf("HTTP POST header send failed, ret=%d\r\n",
               ret);

        W5500_Socket0_Close();

        return HTTP_CLIENT_ERR_SEND;
    }

    ret = W5500_Socket0_Send(
        (const uint8_t *)body,
        (uint16_t)body_len
    );

    if (ret < 0)
    {
        printf("HTTP POST body send failed, ret=%d\r\n",
               ret);

        W5500_Socket0_Close();

        return HTTP_CLIENT_ERR_SEND;
    }

    while (1)
    {
        recv_len = W5500_Socket0_Recv(
            recv_temp,
            sizeof(recv_temp),
            recv_timeout_ms
        );

        if (recv_len > 0)
        {
            if ((total_len + recv_len) >=
                (response_size - 1U))
            {
                uint16_t copy_len;

                copy_len =
                    (response_size - 1U) - total_len;

                if (copy_len > 0U)
                {
                    memcpy(
                        &response[total_len],
                        recv_temp,
                        copy_len
                    );

                    total_len += copy_len;
                    response[total_len] = '\0';
                }

                W5500_Socket0_Close();

                return HTTP_CLIENT_ERR_RESPONSE_FULL;
            }

            memcpy(
                &response[total_len],
                recv_temp,
                (uint16_t)recv_len
            );

            total_len += (uint16_t)recv_len;
            response[total_len] = '\0';
        }
        else if (recv_len == 0)
        {
            break;
        }
        else
        {
            W5500_Socket0_Close();

            return HTTP_CLIENT_ERR_RECV;
        }
    }

    W5500_Socket0_Close();

    return (int)total_len;
}


int http_post(const uint8_t server_ip[4],
              uint16_t server_port,
              const char *host,
              const char *path,
              const char *content_type,
              const char *body,
              char *response,
              uint16_t response_size)
{
    return http_post_timeout(
        server_ip,
        server_port,
        host,
        path,
        content_type,
        body,
        response,
        response_size,
        HTTP_DEFAULT_RECV_TIMEOUT_MS
    );
}
const char *http_get_body(const char *http_response)
{
    const char *body;

    if (http_response == NULL)
    {
        return NULL;
    }

    body = strstr(http_response, "\r\n\r\n");

    if (body == NULL)
    {
        return NULL;
    }

    return body + 4;
}

#ifndef __SIMPLE_JSON_H
#define __SIMPLE_JSON_H

#include <stdint.h>

#define SIMPLE_JSON_OK             0
#define SIMPLE_JSON_ERR_PARAM     -1
#define SIMPLE_JSON_ERR_NOT_FOUND -2
#define SIMPLE_JSON_ERR_FORMAT    -3
#define SIMPLE_JSON_ERR_BUF_SMALL -4

int json_get_string(const char *json,
                    const char *key,
                    char *out,
                    uint16_t out_size);

int json_get_int(const char *json,
                 const char *key,
                 int *value);

#endif
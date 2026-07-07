#include "simple_json.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// 跳过空格
static const char *skip_spaces(const char *p)
{
    while (p != NULL && *p != '\0' && isspace((unsigned char)*p))
    {
        p++;
    }

    return p;
}

// 查找键值对,把json中的每一个指令的起始位置都提取出来
static const char *find_key(const char *json, const char *key)
{
    char key_pattern[64];

    if (json == NULL || key == NULL)
    {
        return NULL;
    }

    if (strlen(key) + 2 >= sizeof(key_pattern))
    {
        return NULL;
    }

    snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key);

    return strstr(json, key_pattern);
}

int json_get_string(const char *json,
                    const char *key,
                    char *out,
                    uint16_t out_size)
{
    const char *p;
    uint16_t len = 0;

    if (json == NULL || key == NULL || out == NULL || out_size == 0)
    {
        return SIMPLE_JSON_ERR_PARAM;
    }

    out[0] = '\0';

    p = find_key(json, key);

    if (p == NULL)
    {
        return SIMPLE_JSON_ERR_NOT_FOUND;
    }

    p += strlen(key) + 2;

    p = skip_spaces(p);

    if (*p != ':')
    {
        return SIMPLE_JSON_ERR_FORMAT;
    }

    p++;
    p = skip_spaces(p);

    if (*p != '"')
    {
        return SIMPLE_JSON_ERR_FORMAT;
    }

    p++;

    while (*p != '\0' && *p != '"')
    {
        if (len >= out_size - 1)
        {
            out[len] = '\0';
            return SIMPLE_JSON_ERR_BUF_SMALL;
        }

        out[len++] = *p++;
    }

    if (*p != '"')
    {
        out[len] = '\0';
        return SIMPLE_JSON_ERR_FORMAT;
    }

    out[len] = '\0';

    return SIMPLE_JSON_OK;
}

int json_get_int(const char *json,
                 const char *key,
                 int *value)
{
    const char *p;
    char *endptr;

    if (json == NULL || key == NULL || value == NULL)
    {
        return SIMPLE_JSON_ERR_PARAM;
    }

    p = find_key(json, key);

    if (p == NULL)
    {
        return SIMPLE_JSON_ERR_NOT_FOUND;
    }

    p += strlen(key) + 2;

    p = skip_spaces(p);

    if (*p != ':')
    {
        return SIMPLE_JSON_ERR_FORMAT;
    }

    p++;
    p = skip_spaces(p);

    *value = (int)strtol(p, &endptr, 10);

    if (endptr == p)
    {
        return SIMPLE_JSON_ERR_FORMAT;
    }

    return SIMPLE_JSON_OK;
}
#ifndef __DEVICE_TOOL_H
#define __DEVICE_TOOL_H

#include <stdint.h>

#define DEVICE_TOOL_OK              0
#define DEVICE_TOOL_ERR_PARAM      -1
#define DEVICE_TOOL_ERR_UNKNOWN    -2
#define DEVICE_TOOL_ERR_RANGE      -3
#define DEVICE_TOOL_ERR_NOT_READY  -4

typedef struct
{
    uint8_t led_on;
    uint8_t fan_on;
    uint8_t buzzer_on;
} DeviceStatus_t;

/* 统一 Tool 执行入口 */
int device_tool_execute(const char *action, int times);

/* 本地设备状态读取接口 */
int device_tool_get_status(DeviceStatus_t *status);

#endif



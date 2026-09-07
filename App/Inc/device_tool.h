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

/* Agent Tool 统一执行入口 */
int device_tool_execute(const char *action, int times);

/* 读取 LED、风扇和蜂鸣器实际状态 */
int device_tool_get_status(DeviceStatus_t *status);

/*
 * 温度自动控制专用接口。
 * 只修改 PE7 风扇状态，不修改手动开启锁定标志。
 */
int device_tool_set_fan_auto(uint8_t on);

/* 查询 Agent 手动开启锁定状态 */
uint8_t device_tool_is_fan_manual_on_locked(void);

#endif

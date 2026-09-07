#ifndef __FREERTOS_DEMO_H
#define __FREERTOS_DEMO_H

#include <stdint.h>

typedef struct
{
    uint8_t human_raw;
    uint8_t human_motion;
    uint8_t human_detected;
    uint8_t human_valid;
} HumanSensorSnapshot_t;

void freertos_start(void);

/*
 * 读取人体传感器共享状态快照。
 *
 * 返回：
 * 1 = 参数有效，已复制快照；
 * 0 = snapshot 为 NULL。
 *
 * human_valid 用于判断快照中的人体状态当前是否有效。
 */
int human_sensor_get_snapshot(HumanSensorSnapshot_t *snapshot);

#endif

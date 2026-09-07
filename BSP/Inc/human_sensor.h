#ifndef __HUMAN_SENSOR_H__
#define __HUMAN_SENSOR_H__

#include <stdint.h>

#define HUMAN_SENSOR_OK          0
#define HUMAN_SENSOR_ERR_PARAM  -1

/*
 * 初始化人体传感器接口。
 * GPIO本身已经由MX_GPIO_Init()完成初始化。
 */
int human_sensor_init(void);

/*
 * 读取PIR原始数字状态。
 *
 * detected:
 * 0 = 当前未检测到人体移动
 * 1 = 当前检测到人体移动
 */
int human_sensor_read_raw(uint8_t *detected);

#endif

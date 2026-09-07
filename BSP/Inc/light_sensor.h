#ifndef __LIGHT_SENSOR_H__
#define __LIGHT_SENSOR_H__

#include <stdint.h>

/* 返回值 */
#define LIGHT_SENSOR_OK                0
#define LIGHT_SENSOR_ERR_PARAM        -1
#define LIGHT_SENSOR_ERR_NOT_READY    -2
#define LIGHT_SENSOR_ERR_CALIBRATION  -3
#define LIGHT_SENSOR_ERR_START        -4
#define LIGHT_SENSOR_ERR_TIMEOUT      -5
#define LIGHT_SENSOR_ERR_STOP         -6

/*
 * 初始化亮度传感器 ADC。
 * 当前主要执行 ADC 单端校准。
 */
int light_sensor_init(void);

/*
 * 读取一次 12 位 ADC 原始值。
 * 成功时 raw 范围为 0～4095。
 */
int light_sensor_read_raw(uint16_t *raw);

#endif

#ifndef __TEMPERATURE_SENSOR_H__
#define __TEMPERATURE_SENSOR_H__

#include <stdint.h>

#define TEMPERATURE_SENSOR_OK                0
#define TEMPERATURE_SENSOR_ERR_PARAM        -1
#define TEMPERATURE_SENSOR_ERR_NOT_READY    -2
#define TEMPERATURE_SENSOR_ERR_CALIBRATION  -3
#define TEMPERATURE_SENSOR_ERR_START        -4
#define TEMPERATURE_SENSOR_ERR_TIMEOUT      -5
#define TEMPERATURE_SENSOR_ERR_STOP         -6

int temperature_sensor_init(void);
int temperature_sensor_read_raw(uint16_t *raw);

#endif
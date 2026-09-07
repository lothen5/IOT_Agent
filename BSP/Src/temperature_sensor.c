#include "temperature_sensor.h"
#include "adc.h"

#define TEMPERATURE_ADC_TIMEOUT_MS  20U

static uint8_t g_temperature_sensor_ready = 0U;

int temperature_sensor_init(void)
{
    HAL_StatusTypeDef hal_ret;

    hal_ret = HAL_ADCEx_Calibration_Start(
        &hadc3,
        ADC_CALIB_OFFSET_LINEARITY,
        ADC_SINGLE_ENDED
    );

    if (hal_ret != HAL_OK)
    {
        g_temperature_sensor_ready = 0U;
        return TEMPERATURE_SENSOR_ERR_CALIBRATION;
    }

    g_temperature_sensor_ready = 1U;
    return TEMPERATURE_SENSOR_OK;
}

int temperature_sensor_read_raw(uint16_t *raw)
{
    HAL_StatusTypeDef hal_ret;
    uint32_t adc_value;

    if (raw == NULL)
    {
        return TEMPERATURE_SENSOR_ERR_PARAM;
    }

    if (g_temperature_sensor_ready == 0U)
    {
        return TEMPERATURE_SENSOR_ERR_NOT_READY;
    }

    hal_ret = HAL_ADC_Start(&hadc3);

    if (hal_ret != HAL_OK)
    {
        return TEMPERATURE_SENSOR_ERR_START;
    }

    hal_ret = HAL_ADC_PollForConversion(
        &hadc3,
        TEMPERATURE_ADC_TIMEOUT_MS
    );

    if (hal_ret != HAL_OK)
    {
        (void)HAL_ADC_Stop(&hadc3);
        return TEMPERATURE_SENSOR_ERR_TIMEOUT;
    }

    adc_value = HAL_ADC_GetValue(&hadc3);

    hal_ret = HAL_ADC_Stop(&hadc3);

    if (hal_ret != HAL_OK)
    {
        return TEMPERATURE_SENSOR_ERR_STOP;
    }

    *raw = (uint16_t)adc_value;

    return TEMPERATURE_SENSOR_OK;
}
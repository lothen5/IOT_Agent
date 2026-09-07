#include "light_sensor.h"
#include "adc.h"

#define LIGHT_SENSOR_ADC_TIMEOUT_MS    20U

static uint8_t g_light_sensor_ready = 0U;

int light_sensor_init(void)
{
    HAL_StatusTypeDef hal_ret;

    /*
     * STM32H743 ADC 在开始正式测量前进行一次校准。
     * 当前 PA3 使用单端输入，因此选择 ADC_SINGLE_ENDED。
     */
    hal_ret = HAL_ADCEx_Calibration_Start(
        &hadc1,
        ADC_CALIB_OFFSET,
        ADC_SINGLE_ENDED
    );

    if (hal_ret != HAL_OK)
    {
        g_light_sensor_ready = 0U;
        return LIGHT_SENSOR_ERR_CALIBRATION;
    }

    g_light_sensor_ready = 1U;

    return LIGHT_SENSOR_OK;
}

int light_sensor_read_raw(uint16_t *raw)
{
    HAL_StatusTypeDef hal_ret;
    uint32_t adc_value;

    if (raw == NULL)
    {
        return LIGHT_SENSOR_ERR_PARAM;
    }

    if (g_light_sensor_ready == 0U)
    {
        return LIGHT_SENSOR_ERR_NOT_READY;
    }

    /*
     * 软件启动一次 Regular Conversion。
     */
    hal_ret = HAL_ADC_Start(&hadc1);

    if (hal_ret != HAL_OK)
    {
        return LIGHT_SENSOR_ERR_START;
    }

    /*
     * 等待本次 ADC 转换完成。
     */
    hal_ret = HAL_ADC_PollForConversion(
        &hadc1,
        LIGHT_SENSOR_ADC_TIMEOUT_MS
    );

    if (hal_ret != HAL_OK)
    {
        (void)HAL_ADC_Stop(&hadc1);
        return LIGHT_SENSOR_ERR_TIMEOUT;
    }

    adc_value = HAL_ADC_GetValue(&hadc1);

    hal_ret = HAL_ADC_Stop(&hadc1);

    if (hal_ret != HAL_OK)
    {
        return LIGHT_SENSOR_ERR_STOP;
    }

    /*
     * 当前 ADC 配置为 12 位，因此正常范围是 0～4095。
     */
    *raw = (uint16_t)adc_value;

    return LIGHT_SENSOR_OK;
}

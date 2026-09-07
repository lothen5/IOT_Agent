#include "human_sensor.h"
#include "main.h"

/*
 * 常见AM312/HC-SR312兼容模块：
 * 检测到人体移动时输出高电平。
 *
 * 若实际测试发现逻辑相反，只需要把这里改为GPIO_PIN_RESET。
 */
#define HUMAN_PIR_ACTIVE_LEVEL  GPIO_PIN_SET

int human_sensor_init(void)
{
    /*
     * PE9已经由CubeMX生成的MX_GPIO_Init()初始化。
     * 当前不需要额外外设初始化。
     */
    return HUMAN_SENSOR_OK;
}

int human_sensor_read_raw(uint8_t *detected)
{
    GPIO_PinState pin_state;

    if (detected == NULL)
    {
        return HUMAN_SENSOR_ERR_PARAM;
    }

    pin_state = HAL_GPIO_ReadPin(
        HUMAN_PIR_GPIO_Port,
        HUMAN_PIR_Pin
    );

    if (pin_state == HUMAN_PIR_ACTIVE_LEVEL)
    {
        *detected = 1U;
    }
    else
    {
        *detected = 0U;
    }

    return HUMAN_SENSOR_OK;
}

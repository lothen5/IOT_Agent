#include "device_tool.h"

#include "gpio.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>
#include <stdio.h>
static int tool_fan_on(void);
static int tool_fan_off(void);
static int tool_buzzer_on(void);
static int tool_buzzer_off(void);
static int tool_get_device_status(void);
/*
 * 当前 LED 使用 PG7。
 *
 * 如果实际测试发现 SET 是熄灭、RESET 是点亮，
 * 只需要交换下面两个宏。
 */
#define DEVICE_LED_PORT         GPIOG
#define DEVICE_LED_PIN          GPIO_PIN_7

#define DEVICE_LED_ON_STATE     GPIO_PIN_SET
#define DEVICE_LED_OFF_STATE    GPIO_PIN_RESET

static DeviceStatus_t g_device_status =
{
    0,
    0,
    0
};

static int tool_led_on(void)
{
    HAL_GPIO_WritePin(DEVICE_LED_PORT,
                      DEVICE_LED_PIN,
                      DEVICE_LED_ON_STATE);

    g_device_status.led_on = 1;

    printf("Tool: led_on\r\n");

    return DEVICE_TOOL_OK;
}

static int tool_led_off(void)
{
    HAL_GPIO_WritePin(DEVICE_LED_PORT,
                      DEVICE_LED_PIN,
                      DEVICE_LED_OFF_STATE);

    g_device_status.led_on = 0;

    printf("Tool: led_off\r\n");

    return DEVICE_TOOL_OK;
}

static int tool_led_blink(int times)
{
    int i;
    uint8_t original_state;

    if (times < 1 || times > 20)
    {
        printf("Tool: led_blink times out of range: %d\r\n",
               times);

        return DEVICE_TOOL_ERR_RANGE;
    }

    original_state = g_device_status.led_on;

    printf("Tool: led_blink, times=%d\r\n", times);

    for (i = 0; i < times; i++)
    {
        HAL_GPIO_WritePin(DEVICE_LED_PORT,
                          DEVICE_LED_PIN,
                          DEVICE_LED_ON_STATE);

        g_device_status.led_on = 1;

        vTaskDelay(pdMS_TO_TICKS(300));

        HAL_GPIO_WritePin(DEVICE_LED_PORT,
                          DEVICE_LED_PIN,
                          DEVICE_LED_OFF_STATE);

        g_device_status.led_on = 0;

        vTaskDelay(pdMS_TO_TICKS(300));
    }

    if (original_state != 0)
    {
        HAL_GPIO_WritePin(DEVICE_LED_PORT,
                          DEVICE_LED_PIN,
                          DEVICE_LED_ON_STATE);

        g_device_status.led_on = 1;
    }
    else
    {
        HAL_GPIO_WritePin(DEVICE_LED_PORT,
                          DEVICE_LED_PIN,
                          DEVICE_LED_OFF_STATE);

        g_device_status.led_on = 0;
    }

    return DEVICE_TOOL_OK;
}
static int tool_not_ready(const char *action)
{
    printf("Tool hardware not ready: %s\r\n", action);

    return DEVICE_TOOL_ERR_NOT_READY;
}

int device_tool_execute(const char *action, int times)
{
    if (action == NULL || action[0] == '\0')
    {
        return DEVICE_TOOL_ERR_PARAM;
    }

    /*
     * 所有 action 必须精确匹配。
     * 不使用 strstr() 等模糊匹配。
     */

    if (strcmp(action, "led_on") == 0)
    {
        return tool_led_on();
    }

    if (strcmp(action, "led_off") == 0)
    {
        return tool_led_off();
    }

    if (strcmp(action, "led_blink") == 0)
    {
        return tool_led_blink(times);
    }

    if (strcmp(action, "fan_on") == 0)
    {
        return tool_fan_on();
    }

    if (strcmp(action, "fan_off") == 0)
    {
        return tool_fan_off();
    }

    if (strcmp(action, "buzzer_on") == 0)
    {
        return tool_buzzer_on();
    }

    if (strcmp(action, "buzzer_off") == 0)
    {
        return tool_buzzer_off();
    }

    if (strcmp(action, "get_device_status") == 0)
    {
        return tool_get_device_status();
    }


    if (strcmp(action, "none") == 0)
    {
        printf("Tool: none, no hardware operation\r\n");

        return DEVICE_TOOL_OK;
    }

    printf("Unknown tool action: %s\r\n", action);

    return DEVICE_TOOL_ERR_UNKNOWN;
}

static int tool_fan_on(void)
{
    HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port,
                      FAN_CTRL_Pin,
                      GPIO_PIN_SET);

    g_device_status.fan_on = 1;
    printf("Tool: fan_on\r\n");

    return DEVICE_TOOL_OK;
}

static int tool_fan_off(void)
{
    HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port,
                      FAN_CTRL_Pin,
                      GPIO_PIN_RESET);

    g_device_status.fan_on = 0;
    printf("Tool: fan_off\r\n");

    return DEVICE_TOOL_OK;
}

static int tool_buzzer_on(void)
{
    HAL_GPIO_WritePin(BUZZER_CTRL_GPIO_Port,
                      BUZZER_CTRL_Pin,
                      GPIO_PIN_SET);

    g_device_status.buzzer_on = 1;
    printf("Tool: buzzer_on\r\n");

    return DEVICE_TOOL_OK;
}

static int tool_buzzer_off(void)
{
    HAL_GPIO_WritePin(BUZZER_CTRL_GPIO_Port,
                      BUZZER_CTRL_Pin,
                      GPIO_PIN_RESET);

    g_device_status.buzzer_on = 0;
    printf("Tool: buzzer_off\r\n");

    return DEVICE_TOOL_OK;
}

// 本地设备状态读取接口
int device_tool_get_status(DeviceStatus_t *status)
{
    if (status == NULL)
    {
        return DEVICE_TOOL_ERR_PARAM;
    }

    *status = g_device_status;

    return DEVICE_TOOL_OK;
}

static int tool_get_device_status(void)
{
    DeviceStatus_t status;
    int ret;

    ret = device_tool_get_status(&status);

    if (ret != DEVICE_TOOL_OK)
    {
        return ret;
    }

    printf("Tool: get_device_status\r\n");
    printf("LED=%s, FAN=%s, BUZZER=%s\r\n",
           status.led_on ? "ON" : "OFF",
           status.fan_on ? "ON" : "OFF",
           status.buzzer_on ? "ON" : "OFF");

    return DEVICE_TOOL_OK;
}



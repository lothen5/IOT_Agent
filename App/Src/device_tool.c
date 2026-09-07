#include "device_tool.h"

#include "gpio.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

/* 私有 Tool 函数前置声明 */
static int tool_led_on(void);
static int tool_led_off(void);
static int tool_led_blink(int times);
static int tool_fan_on(void);
static int tool_fan_off(void);
static int tool_buzzer_on(void);
static int tool_buzzer_off(void);
static int tool_get_device_status(void);

/* PE13 LIGHT_LED：低电平点亮 */
#define DEVICE_LED_PORT         LIGHT_LED_GPIO_Port
#define DEVICE_LED_PIN          LIGHT_LED_Pin
#define DEVICE_LED_ON_STATE     GPIO_PIN_RESET
#define DEVICE_LED_OFF_STATE    GPIO_PIN_SET

/* PE7 FAN_CTRL：高电平开启 */
#define DEVICE_FAN_PORT         FAN_CTRL_GPIO_Port
#define DEVICE_FAN_PIN          FAN_CTRL_Pin
#define DEVICE_FAN_ON_STATE     GPIO_PIN_SET
#define DEVICE_FAN_OFF_STATE    GPIO_PIN_RESET

/* PE8 BUZZER_CTRL：高电平开启 */
#define DEVICE_BUZZER_PORT      BUZZER_CTRL_GPIO_Port
#define DEVICE_BUZZER_PIN       BUZZER_CTRL_Pin
#define DEVICE_BUZZER_ON_STATE  GPIO_PIN_SET
#define DEVICE_BUZZER_OFF_STATE GPIO_PIN_RESET

static DeviceStatus_t g_device_status =
{
    0U,
    0U,
    0U
};

/*
 * Agent 手动开启锁定：
 * 0：温度迟滞逻辑可以自动关闭风扇；
 * 1：温度迟滞逻辑不得自动关闭风扇。
 */
static uint8_t g_fan_manual_on_lock = 0U;

static void device_led_set(uint8_t on)
{
    HAL_GPIO_WritePin(
        DEVICE_LED_PORT,
        DEVICE_LED_PIN,
        (on != 0U) ? DEVICE_LED_ON_STATE : DEVICE_LED_OFF_STATE
    );

    g_device_status.led_on = (on != 0U) ? 1U : 0U;
}

static uint8_t device_led_is_on(void)
{
    GPIO_PinState pin_state;

    pin_state = HAL_GPIO_ReadPin(
        DEVICE_LED_PORT,
        DEVICE_LED_PIN
    );

    return (pin_state == DEVICE_LED_ON_STATE) ? 1U : 0U;
}

static void device_fan_set(uint8_t on)
{
    HAL_GPIO_WritePin(
        DEVICE_FAN_PORT,
        DEVICE_FAN_PIN,
        (on != 0U) ? DEVICE_FAN_ON_STATE : DEVICE_FAN_OFF_STATE
    );

    g_device_status.fan_on = (on != 0U) ? 1U : 0U;
}

static uint8_t device_fan_is_on(void)
{
    GPIO_PinState pin_state;

    pin_state = HAL_GPIO_ReadPin(
        DEVICE_FAN_PORT,
        DEVICE_FAN_PIN
    );

    return (pin_state == DEVICE_FAN_ON_STATE) ? 1U : 0U;
}

static void device_buzzer_set(uint8_t on)
{
    HAL_GPIO_WritePin(
        DEVICE_BUZZER_PORT,
        DEVICE_BUZZER_PIN,
        (on != 0U) ? DEVICE_BUZZER_ON_STATE : DEVICE_BUZZER_OFF_STATE
    );

    g_device_status.buzzer_on = (on != 0U) ? 1U : 0U;
}

static uint8_t device_buzzer_is_on(void)
{
    GPIO_PinState pin_state;

    pin_state = HAL_GPIO_ReadPin(
        DEVICE_BUZZER_PORT,
        DEVICE_BUZZER_PIN
    );

    return (pin_state == DEVICE_BUZZER_ON_STATE) ? 1U : 0U;
}

static int tool_led_on(void)
{
    device_led_set(1U);

    printf("Tool: led_on, PE13=LOW, LED=ON\r\n");

    return DEVICE_TOOL_OK;
}

static int tool_led_off(void)
{
    device_led_set(0U);

    printf("Tool: led_off, PE13=HIGH, LED=OFF\r\n");

    return DEVICE_TOOL_OK;
}

static int tool_led_blink(int times)
{
    int i;
    uint8_t original_led_on;

    if (times < 1 || times > 20)
    {
        printf(
            "Tool: led_blink times out of range: %d\r\n",
            times
        );

        return DEVICE_TOOL_ERR_RANGE;
    }

    original_led_on = device_led_is_on();

    printf(
        "Tool: led_blink, times=%d, original=%s\r\n",
        times,
        original_led_on ? "ON" : "OFF"
    );

    for (i = 0; i < times; i++)
    {
        device_led_set(1U);
        vTaskDelay(pdMS_TO_TICKS(300U));

        device_led_set(0U);
        vTaskDelay(pdMS_TO_TICKS(300U));
    }

    device_led_set(original_led_on);

    return DEVICE_TOOL_OK;
}

static int tool_fan_on(void)
{
    /* Agent 手动开启时建立锁定。 */
    g_fan_manual_on_lock = 1U;

    device_fan_set(1U);

    printf("Tool: fan_on, manual lock=ON\r\n");

    return DEVICE_TOOL_OK;
}

static int tool_fan_off(void)
{
    /* Agent 手动关闭时解除锁定并关闭风扇。 */
    g_fan_manual_on_lock = 0U;

    device_fan_set(0U);

    printf("Tool: fan_off, manual lock=OFF\r\n");

    return DEVICE_TOOL_OK;
}

static int tool_buzzer_on(void)
{
    device_buzzer_set(1U);

    printf("Tool: buzzer_on\r\n");

    return DEVICE_TOOL_OK;
}

static int tool_buzzer_off(void)
{
    device_buzzer_set(0U);

    printf("Tool: buzzer_off\r\n");

    return DEVICE_TOOL_OK;
}

int device_tool_set_fan_auto(uint8_t on)
{
    /*
     * 温度自动控制只改变 PE7 的实际输出，
     * 不修改 g_fan_manual_on_lock。
     */
    device_fan_set(on);

    return DEVICE_TOOL_OK;
}

uint8_t device_tool_is_fan_manual_on_locked(void)
{
    return g_fan_manual_on_lock;
}

int device_tool_get_status(DeviceStatus_t *status)
{
    if (status == NULL)
    {
        return DEVICE_TOOL_ERR_PARAM;
    }

    /* 所有状态均以实际 GPIO 为准。 */
    g_device_status.led_on = device_led_is_on();
    g_device_status.fan_on = device_fan_is_on();
    g_device_status.buzzer_on = device_buzzer_is_on();

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
    printf(
        "LED=%s, FAN=%s, BUZZER=%s\r\n",
        status.led_on ? "ON" : "OFF",
        status.fan_on ? "ON" : "OFF",
        status.buzzer_on ? "ON" : "OFF"
    );

    return DEVICE_TOOL_OK;
}

int device_tool_execute(const char *action, int times)
{
    if (action == NULL || action[0] == '\0')
    {
        return DEVICE_TOOL_ERR_PARAM;
    }

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

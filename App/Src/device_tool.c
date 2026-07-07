#include "device_tool.h"

#include "gpio.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>
#include <stdio.h>

static void led_blink(int times)
{
    int i;

    if (times <= 0)
    {
        times = 1;
    }

    if (times > 20)
    {
        times = 20;
    }

    printf("Tool: led_blink, times=%d\r\n", times);

    for (i = 0; i < times; i++)
    {
        HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_7);
        vTaskDelay(pdMS_TO_TICKS(300));

        HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_7);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

int device_tool_execute(const char *action, int times)
{
    if (action == NULL)
    {
        return DEVICE_TOOL_ERR_PARAM;
    }

    if (strcmp(action, "led_blink") == 0)
    {
        led_blink(times);
        return DEVICE_TOOL_OK;
    }

    printf("Unknown tool action: %s\r\n", action);

    return DEVICE_TOOL_ERR_UNKNOWN;
}

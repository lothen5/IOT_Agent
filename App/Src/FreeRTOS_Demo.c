#include "FreeRTOS_Demo.h"
/* freertos相关的头文件，必须的 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
/* 需要用到的其他头文件 */
#include "usart.h"
#include "string.h"
#include <stdio.h>
#include "gpio.h"
#include "W5500.h"
#include "http_client.h"
#include "simple_json.h"
#include "device_tool.h"



/* 启动任务函数 */
#define START_TASK_PRIORITY 1
#define START_TASK_STACK_DEPTH 128
TaskHandle_t start_task_handler;
void Start_Task(void *pvParameters);

#define TASK1_PRIORITY 2
#define TASK1_STACK_DEPTH 256
TaskHandle_t ledtask_handle;
void task1(void *pvParameters);

#define TASK2_PRIORITY 3
#define TASK2_STACK_DEPTH 512
TaskHandle_t uarttask_handle;
void task2(void *pvParameters);

#define TASK_W5500_PRIORITY 2
#define TASK_W5500_STACK_DEPTH 512
TaskHandle_t task_w5500_handle;
void task_w5500(void *pvParameters);



void freertos_start(void)
{
    /* 启动任务 */
    xTaskCreate(
        Start_Task, 
        "Start_Task", 
        START_TASK_STACK_DEPTH, 
        NULL, 
        START_TASK_PRIORITY, 
        &start_task_handler
    );
    vTaskStartScheduler();
}

void Start_Task(void *pvParameters)
{
    taskENTER_CRITICAL();
    
    xTaskCreate(
        task1, 
        "task1", 
        TASK1_STACK_DEPTH, 
        NULL, 
        TASK1_PRIORITY, 
        &ledtask_handle
    );
    xTaskCreate(
        task2, 
        "task2", 
        TASK2_STACK_DEPTH, 
        NULL, 
        TASK2_PRIORITY, 
        &uarttask_handle
    );
    xTaskCreate(
        task_w5500, 
        "task_w5500", 
        TASK_W5500_STACK_DEPTH, 
        NULL, 
        TASK_W5500_PRIORITY, 
        &task_w5500_handle
    );

    /* 启动任务只需要执行一次即可，用完就删除自己 */
    

    /* 退出临界区 */
    taskEXIT_CRITICAL();
    vTaskDelete(NULL);
}

void task1(void *pvParameters)
{
    while (1)
    {
        HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_7);
        vTaskDelay(pdMS_TO_TICKS(5000));
       
    }
}

void task2(void *pvParameters)
{
    while (1)
    {
        printf("FreeRTOS running\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task_w5500(void *pvParameters)
{
    uint8_t version;
    uint8_t server_ip[4] = {192, 168, 1, 100};

    static char response[1024];

    const char *json_body =
        "{\"input\":\"Blink LED 10 times\"}";

    const char *body_ptr;

    char action[32];
    char msg[64];

    int times = 0;
    int ret;
    int tool_ret;

    (void)pvParameters;

    printf("W5500 network test start\r\n");

    W5500_HardReset();

    version = W5500_ReadVersion();

    printf("W5500 VERSIONR = 0x%02X\r\n", version);

    if (version != W5500_VERSION_VALUE)
    {
        printf("W5500 SPI ERROR\r\n");

        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    printf("W5500 SPI OK\r\n");

    W5500_NetworkConfig();
    W5500_PrintNetworkInfo();

    while (!W5500_IsLinkUp())
    {
        printf("W5500 Link DOWN\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    printf("W5500 Link UP\r\n");

    vTaskDelay(pdMS_TO_TICKS(2000));

    ret = http_post(server_ip,
                    8080,
                    "192.168.1.100",
                    "/api/test",
                    "application/json",
                    json_body,
                    response,
                    sizeof(response));

    if (ret >= 0)
    {
        printf("HTTP POST full response:\r\n");
        printf("%s\r\n", response);

        body_ptr = http_get_body(response);

        if (body_ptr != NULL)
        {
            printf("HTTP response body:\r\n");
            printf("%s\r\n", body_ptr);

            if (json_get_string(body_ptr, "action", action, sizeof(action)) == SIMPLE_JSON_OK)
            {
                printf("JSON action = %s\r\n", action);

                if (json_get_int(body_ptr, "times", &times) == SIMPLE_JSON_OK)
                {
                    printf("JSON times = %d\r\n", times);
                }
                else
                {
                    printf("JSON times parse failed, use default 1\r\n");
                    times = 1;
                }

                if (json_get_string(body_ptr, "msg", msg, sizeof(msg)) == SIMPLE_JSON_OK)
                {
                    printf("JSON msg = %s\r\n", msg);
                }
                else
                {
                    printf("JSON msg parse failed\r\n");
                }

                tool_ret = device_tool_execute(action, times);

                if (tool_ret == DEVICE_TOOL_OK)
                {
                    printf("Tool execute OK\r\n");
                }
                else
                {
                    printf("Tool execute failed, ret=%d\r\n", tool_ret);
                }
            }
            else
            {
                printf("JSON action parse failed\r\n");
            }
        }
        else
        {
            printf("HTTP body not found\r\n");
        }
    }
    else
    {
        printf("http_post failed, ret=%d\r\n", ret);
    }

    while (1)
    {
        printf("W5500 task alive\r\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}





void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    static StaticTask_t idle_task_tcb;
    static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    static StaticTask_t timer_task_tcb;
    static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer = &timer_task_tcb;
    *ppxTimerTaskStackBuffer = timer_task_stack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

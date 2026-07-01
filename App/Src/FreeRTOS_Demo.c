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

#define w5500task_PRIORITY 2
#define w5500task_STACK_DEPTH 512
TaskHandle_t w5500task_handle;
void w5500task(void *pvParameters);



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
        w5500task, 
        "w5500task", 
        w5500task_STACK_DEPTH, 
        NULL, 
        w5500task_PRIORITY, 
        &w5500task_handle
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
        vTaskDelay(pdMS_TO_TICKS(500));
       
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

void w5500task(void *pvParameters)
{
    uint8_t version;

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

    printf("Please ping 192.168.1.123 from PC.\r\n");

    while (!W5500_IsLinkUp())
    {
        printf("W5500 Link DOWN\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    printf("W5500 Link UP\r\n");

    vTaskDelay(pdMS_TO_TICKS(5000));

    W5500_HTTP_GET_Test();

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

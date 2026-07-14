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

#define UART_JSON_ESCAPED_BUFFER_SIZE \
    ((UART_COMMAND_BUFFER_SIZE * 6U) + 1U)

#define UART_JSON_REQUEST_BUFFER_SIZE \
    (UART_JSON_ESCAPED_BUFFER_SIZE + 16U)

/*
 * 将串口命令转义成合法的 JSON 字符串内容。
 *
 * UTF-8 中文字节保持不变。
 * 双引号、反斜杠和控制字符进行 JSON 转义。
 */
static int json_escape_string(const char *source,
                              char *destination,
                              uint32_t destination_size)
{
    const uint8_t *input;
    uint8_t value;
    uint32_t output_length;

    if (source == NULL ||
        destination == NULL ||
        destination_size == 0U)
    {
        return -1;
    }

    input = (const uint8_t *)source;
    output_length = 0U;

    while (*input != 0U)
    {
        value = *input;
        input++;

        if (value == '\"' || value == '\\')
        {
            if ((output_length + 2U) >= destination_size)
            {
                return -2;
            }

            destination[output_length++] = '\\';
            destination[output_length++] = (char)value;
        }
        else if (value == '\b')
        {
            if ((output_length + 2U) >= destination_size)
            {
                return -2;
            }

            destination[output_length++] = '\\';
            destination[output_length++] = 'b';
        }
        else if (value == '\f')
        {
            if ((output_length + 2U) >= destination_size)
            {
                return -2;
            }

            destination[output_length++] = '\\';
            destination[output_length++] = 'f';
        }
        else if (value == '\n')
        {
            if ((output_length + 2U) >= destination_size)
            {
                return -2;
            }

            destination[output_length++] = '\\';
            destination[output_length++] = 'n';
        }
        else if (value == '\r')
        {
            if ((output_length + 2U) >= destination_size)
            {
                return -2;
            }

            destination[output_length++] = '\\';
            destination[output_length++] = 'r';
        }
        else if (value == '\t')
        {
            if ((output_length + 2U) >= destination_size)
            {
                return -2;
            }

            destination[output_length++] = '\\';
            destination[output_length++] = 't';
        }
        else if (value < 0x20U)
        {
            static const char hex_digits[] =
                "0123456789ABCDEF";

            if ((output_length + 6U) >= destination_size)
            {
                return -2;
            }

            destination[output_length++] = '\\';
            destination[output_length++] = 'u';
            destination[output_length++] = '0';
            destination[output_length++] = '0';
            destination[output_length++] =
                hex_digits[(value >> 4) & 0x0FU];
            destination[output_length++] =
                hex_digits[value & 0x0FU];
        }
        else
        {
            /*
             * 普通 ASCII 和 UTF-8 多字节内容均原样复制。
             */
            if ((output_length + 1U) >= destination_size)
            {
                return -2;
            }

            destination[output_length++] = (char)value;
        }
    }

    destination[output_length] = '\0';

    return (int)output_length;
}



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
    
    // xTaskCreate(
    //     task1, 
    //     "task1", 
    //     TASK1_STACK_DEPTH, 
    //     NULL, 
    //     TASK1_PRIORITY, 
    //     &ledtask_handle
    // );
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
    (void)pvParameters;

    printf("UART monitor task ready\r\n");

    while (1)
    {
        /*
         * 这里只处理串口错误状态。
         *
         * 不再调用 uart_command_get()。
         * 完整命令由 task_w5500 唯一消费。
         */
        if (uart_command_take_overflow() != 0U)
        {
            printf("[UART ERROR] command too long, "
                   "line discarded\r\n");
        }

        if (uart_command_take_dropped() != 0U)
        {
            printf("[UART ERROR] command dropped, "
                   "previous command pending\r\n");
        }

        if (uart_command_take_rx_error() != 0U)
        {
            printf("[UART ERROR] receive error, "
                   "receiver restarted\r\n");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void task_w5500(void *pvParameters)
{
    uint8_t version;
    uint8_t server_ip[4] = {192, 168, 1, 100};

    static char uart_command[UART_COMMAND_BUFFER_SIZE];
    static char escaped_command[
        UART_JSON_ESCAPED_BUFFER_SIZE
    ];
    static char request_body[
        UART_JSON_REQUEST_BUFFER_SIZE
    ];

    static char response[1024];
    static char report_response[512];
    static char result_body[320];

    /*
     * 服务端如果返回中文 msg，
     * 需要比原来的 64 字节留出更多 UTF-8 空间。
     */
    static char msg[160];

    const char *body_ptr;
    const char *result_msg;
    const char *result_status;
    const char *led_state;
    const char *fan_state;
    const char *buzzer_state;

    char action[32];

    DeviceStatus_t device_status;

    int command_length;
    int escape_ret;
    int request_format_ret;
    int ret;
    int times;
    int tool_ret;
    int status_ret;
    int result_format_ret;
    int include_device_status;

    (void)pvParameters;

    printf("W5500 network task start\r\n");

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
    printf("[UART READY] enter command and press Enter\r\n");

    while (1)
    {
        /*
         * 链路断开时暂不消费串口命令。
         * 已完成的命令会继续保留在 UART ready 缓冲区中。
         */
        if (!W5500_IsLinkUp())
        {
            printf("[NET][ERROR] W5500 Link DOWN\r\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (uart_command_is_ready() == 0U)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        command_length = uart_command_get(
            uart_command,
            (uint16_t)sizeof(uart_command)
        );

        if (command_length == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (command_length < 0)
        {
            printf("[UART ERROR] command get failed, "
                   "ret=%d\r\n",
                   command_length);

            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        printf("\r\n");
        printf("================================\r\n");
        printf("[UART CMD] %s\r\n", uart_command);
        printf("[UART CMD] byte length=%d\r\n",
               command_length);
        printf("================================\r\n");

        /*
         * 将用户输入转义为合法 JSON 字符串。
         * UTF-8 中文字节不会被转换。
         */
        escape_ret = json_escape_string(
            uart_command,
            escaped_command,
            sizeof(escaped_command)
        );

        if (escape_ret < 0)
        {
            printf("[JSON ERROR] command escape failed, "
                   "ret=%d\r\n",
                   escape_ret);

            printf("[UART READY] enter next command\r\n");
            continue;
        }

        request_format_ret = snprintf(
            request_body,
            sizeof(request_body),
            "{\"input\":\"%s\"}",
            escaped_command
        );

        if (request_format_ret <= 0 ||
            request_format_ret >=
                (int)sizeof(request_body))
        {
            printf("[JSON ERROR] request body build failed, "
                   "required=%d, buffer=%d\r\n",
                   request_format_ret,
                   (int)sizeof(request_body));

            printf("[UART READY] enter next command\r\n");
            continue;
        }

        printf("[HTTP] POST /api/test\r\n");
        printf("[HTTP] request body: %s\r\n",
               request_body);

        response[0] = '\0';

        ret = http_post(server_ip,
                        8080,
                        "192.168.1.100",
                        "/api/test",
                        "application/json",
                        request_body,
                        response,
                        sizeof(response));

        if (ret < 0)
        {
            printf("[HTTP ERROR] /api/test failed, "
                   "ret=%d\r\n",
                   ret);

            printf("[UART READY] enter next command\r\n");
            continue;
        }

        printf("HTTP POST full response:\r\n");
        printf("%s\r\n", response);

        body_ptr = http_get_body(response);

        if (body_ptr == NULL)
        {
            printf("[HTTP ERROR] response body not found\r\n");
            printf("[UART READY] enter next command\r\n");
            continue;
        }

        printf("HTTP response body:\r\n");
        printf("%s\r\n", body_ptr);

        action[0] = '\0';
        msg[0] = '\0';
        times = 0;

        ret = json_get_string(body_ptr,
                              "action",
                              action,
                              sizeof(action));

        if (ret != SIMPLE_JSON_OK)
        {
            printf("[JSON ERROR] action parse failed, "
                   "ret=%d\r\n",
                   ret);

            printf("[UART READY] enter next command\r\n");
            continue;
        }

        printf("JSON action = %s\r\n", action);

        ret = json_get_int(body_ptr,
                           "times",
                           &times);

        if (ret == SIMPLE_JSON_OK)
        {
            printf("JSON times = %d\r\n", times);
        }
        else if (strcmp(action, "led_blink") == 0)
        {
            printf("JSON times missing, "
                   "led_blink uses default 1\r\n");

            times = 1;
        }
        else
        {
            times = 0;
        }

        ret = json_get_string(body_ptr,
                              "msg",
                              msg,
                              sizeof(msg));

        if (ret == SIMPLE_JSON_OK)
        {
            printf("JSON msg = %s\r\n", msg);
        }
        else
        {
            printf("JSON msg parse failed, ret=%d\r\n",
                   ret);
        }

        /*
         * 所有硬件动作仍然统一经过 device_tool_execute()。
         */
        tool_ret = device_tool_execute(action, times);

        include_device_status = 0;
        status_ret = DEVICE_TOOL_OK;

        memset(&device_status,
               0,
               sizeof(device_status));

        /*
         * get_device_status 成功后获取状态，
         * 用于 /api/tool_result 回调。
         */
        if ((strcmp(action, "get_device_status") == 0) &&
            (tool_ret == DEVICE_TOOL_OK))
        {
            status_ret =
                device_tool_get_status(&device_status);

            if (status_ret == DEVICE_TOOL_OK)
            {
                include_device_status = 1;
            }
            else
            {
                tool_ret = status_ret;
                include_device_status = 0;

                printf("Device status read failed, "
                       "ret=%d\r\n",
                       status_ret);
            }
        }

        switch (tool_ret)
        {
            case DEVICE_TOOL_OK:
                if (strcmp(action,
                           "get_device_status") == 0)
                {
                    result_msg =
                        "device status queried";
                }
                else
                {
                    result_msg =
                        "tool executed on stm32";
                }
                break;

            case DEVICE_TOOL_ERR_PARAM:
                result_msg =
                    "invalid tool parameter";
                break;

            case DEVICE_TOOL_ERR_UNKNOWN:
                result_msg =
                    "unknown tool action";
                break;

            case DEVICE_TOOL_ERR_RANGE:
                result_msg =
                    "tool parameter out of range";
                break;

            case DEVICE_TOOL_ERR_NOT_READY:
                result_msg =
                    "tool hardware not ready";
                break;

            default:
                result_msg =
                    "tool execution failed";
                break;
        }

        if (tool_ret == DEVICE_TOOL_OK)
        {
            result_status = "ok";
            printf("Tool execute OK\r\n");
        }
        else
        {
            if (strcmp(action,
                       "get_device_status") == 0)
            {
                result_status = "error";
            }
            else
            {
                result_status = "failed";
            }

            printf("Tool execute failed, ret=%d\r\n",
                   tool_ret);
        }

        if (include_device_status != 0)
        {
            led_state =
                device_status.led_on ? "on" : "off";

            fan_state =
                device_status.fan_on ? "on" : "off";

            buzzer_state =
                device_status.buzzer_on ? "on" : "off";

            result_format_ret = snprintf(
                result_body,
                sizeof(result_body),
                "{\"action\":\"%s\","
                "\"times\":%d,"
                "\"status\":\"%s\","
                "\"ret\":%d,"
                "\"msg\":\"%s\","
                "\"led\":\"%s\","
                "\"fan\":\"%s\","
                "\"buzzer\":\"%s\"}",
                action,
                times,
                result_status,
                tool_ret,
                result_msg,
                led_state,
                fan_state,
                buzzer_state
            );
        }
        else
        {
            result_format_ret = snprintf(
                result_body,
                sizeof(result_body),
                "{\"action\":\"%s\","
                "\"times\":%d,"
                "\"status\":\"%s\","
                "\"ret\":%d,"
                "\"msg\":\"%s\"}",
                action,
                times,
                result_status,
                tool_ret,
                result_msg
            );
        }

        if (result_format_ret <= 0 ||
            result_format_ret >=
                (int)sizeof(result_body))
        {
            printf("[JSON ERROR] tool result build failed, "
                   "required=%d, buffer=%d\r\n",
                   result_format_ret,
                   (int)sizeof(result_body));

            printf("[UART READY] enter next command\r\n");
            continue;
        }

        printf("Report tool result:\r\n");
        printf("%s\r\n", result_body);

        report_response[0] = '\0';

        ret = http_post(server_ip,
                        8080,
                        "192.168.1.100",
                        "/api/tool_result",
                        "application/json",
                        result_body,
                        report_response,
                        sizeof(report_response));

        if (ret >= 0)
        {
            printf("Tool result report response:\r\n");
            printf("%s\r\n", report_response);
        }
        else
        {
            printf("[HTTP ERROR] tool result report failed, "
                   "ret=%d\r\n",
                   ret);
        }

        printf("[UART READY] enter next command\r\n");
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

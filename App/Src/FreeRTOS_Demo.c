#include "FreeRTOS_Demo.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "usart.h"
#include "gpio.h"
#include "W5500.h"
#include "http_client.h"
#include "simple_json.h"
#include "device_tool.h"
#include "light_sensor.h"
#include "temperature_sensor.h"
#include "human_sensor.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 启动任务 */
#define START_TASK_PRIORITY       1
#define START_TASK_STACK_DEPTH   128
TaskHandle_t start_task_handler;
void Start_Task(void *pvParameters);

/* 保留的 task1，目前不创建 */
#define TASK1_PRIORITY            2
#define TASK1_STACK_DEPTH        256
TaskHandle_t ledtask_handle;
void task1(void *pvParameters);

/* 串口错误监控任务 */
#define TASK2_PRIORITY            3
#define TASK2_STACK_DEPTH        512
TaskHandle_t uarttask_handle;
void task2(void *pvParameters);

/* W5500、Agent 和遥测任务 */
#define TASK_W5500_PRIORITY       2
#define TASK_W5500_STACK_DEPTH   512
TaskHandle_t task_w5500_handle;
void task_w5500(void *pvParameters);

/* 温度任务 */
#define TASK_TEMPERATURE_PRIORITY      2
#define TASK_TEMPERATURE_STACK_DEPTH 256
TaskHandle_t temperature_task_handle;
void task_temperature_sensor(void *pvParameters);

/* 光照任务 */
#define TASK_LIGHT_PRIORITY       2
#define TASK_LIGHT_STACK_DEPTH   256
TaskHandle_t light_task_handle;
void task_light_sensor(void *pvParameters);

/* 人体传感器任务 */
#define TASK_HUMAN_PRIORITY       2
#define TASK_HUMAN_STACK_DEPTH    256
TaskHandle_t human_task_handle;
void task_human_sensor(void *pvParameters);

#define LIGHT_TELEMETRY_PERIOD_MS          2000U
#define LIGHT_TELEMETRY_RECV_TIMEOUT_MS     500U
#define LIGHT_TELEMETRY_JSON_SIZE           320U
#define LIGHT_TELEMETRY_RESPONSE_SIZE       512U

#define UART_JSON_ESCAPED_BUFFER_SIZE \
    ((UART_COMMAND_BUFFER_SIZE * 6U) + 1U)

#define UART_JSON_REQUEST_BUFFER_SIZE \
    (UART_JSON_ESCAPED_BUFFER_SIZE + 16U)

/* 光照传感器 */
#define LIGHT_FILTER_SIZE          8U
#define LIGHT_SAMPLE_PERIOD_MS   100U
#define LIGHT_BRIGHT_THRESHOLD   200U
#define LIGHT_DARK_THRESHOLD    2000U
#define LIGHT_LED_ON_THRESHOLD  2000U
#define LIGHT_LED_OFF_THRESHOLD 1000U
#define LIGHT_LED_ON_LEVEL       GPIO_PIN_RESET
#define LIGHT_LED_OFF_LEVEL      GPIO_PIN_SET

/* 温度传感器 */
#define TEMP_FILTER_SIZE          8U
#define TEMP_SAMPLE_PERIOD_MS   500U
#define TEMP_HOT_THRESHOLD     1800U
#define TEMP_COLD_THRESHOLD    2200U
#define TEMP_ADC_VALID_MIN      100U
#define TEMP_ADC_VALID_MAX     4000U

/* 温度升高时 ADC 值下降。 */
#define TEMP_FAN_ON_THRESHOLD   1800U
#define TEMP_FAN_OFF_THRESHOLD  1950U

/* 人体传感器 */
#define HUMAN_SAMPLE_PERIOD_MS       100U
#define HUMAN_STABLE_SAMPLE_COUNT      3U
#define HUMAN_WARMUP_TIME_MS       30000U
#define HUMAN_LOG_PERIOD_COUNT        20U
/*
 * 最近一次检测到人体活动后，
 * 继续保持“推断有人”状态30秒。
 */
#define HUMAN_HOLD_TIME_MS          30000U
typedef enum
{
    TEMPERATURE_LEVEL_INVALID = 0,
    TEMPERATURE_LEVEL_COLD,
    TEMPERATURE_LEVEL_NORMAL,
    TEMPERATURE_LEVEL_HOT
} TemperatureLevel_t;

typedef struct
{
    uint16_t raw;
    uint16_t avg;
    TemperatureLevel_t level;
    uint8_t valid;
} TemperatureSensorSharedData_t;

typedef struct
{
    uint16_t raw;
    uint16_t avg;
    uint8_t valid;
} LightSensorSharedData_t;

typedef struct
{
    uint16_t light_raw;
    uint16_t light_avg;
    const char *light_level;
    const char *led_state;

    uint16_t temperature_raw;
    uint16_t temperature_avg;
    const char *temperature_level;
    const char *fan_state;

    uint32_t uptime_ms;

    uint8_t human_raw;
    uint8_t human_motion;
    uint8_t human_detected;
    uint8_t human_valid;
} LightTelemetrySnapshot_t;

static volatile TemperatureSensorSharedData_t g_temperature_shared_data =
{
    0U,
    0U,
    TEMPERATURE_LEVEL_INVALID,
    0U
};

static volatile LightSensorSharedData_t g_light_shared_data =
{
    0U,
    0U,
    0U
};

static volatile HumanSensorSnapshot_t g_human_shared_data =
{
    0U,
    0U,
    0U,
    0U
};

static uint8_t g_light_led_on = 0U;

static int light_telemetry_get_snapshot(
    LightTelemetrySnapshot_t *snapshot
)
{
    uint16_t light_raw_snapshot;
    uint16_t light_avg_snapshot;

    uint16_t temperature_raw_snapshot;
    uint16_t temperature_avg_snapshot;
    TemperatureLevel_t temperature_level_snapshot;

    HumanSensorSnapshot_t human_snapshot = {0};

    DeviceStatus_t device_status = {0};

    int status_ret;

    if (snapshot == NULL)
    {
        return 0;
    }

    /*
     * 光照任务必须已经产生第一组平均值。
     */
    if (light_sensor_get_latest_data(
            &light_raw_snapshot,
            &light_avg_snapshot
        ) == 0)
    {
        return 0;
    }

    /*
     * 温度任务必须已经产生第一组平均值。
     */
    if (temperature_sensor_get_latest_data(
            &temperature_raw_snapshot,
            &temperature_avg_snapshot,
            &temperature_level_snapshot
        ) == 0)
    {
        return 0;
    }

    /*
     * 人体状态直接读取现有共享快照。
     *
     * human_valid 可以为 0。
     * 预热或读取异常期间仍然允许上传，
     * 但服务端必须根据 human_valid 判断数据是否有效。
     */
    if (human_sensor_get_snapshot(
            &human_snapshot
        ) == 0)
    {
        return 0;
    }

    /*
     * LED、风扇继续使用 device_tool 的统一状态来源。
     */
    status_ret = device_tool_get_status(&device_status);

    if (status_ret != DEVICE_TOOL_OK)
    {
        return 0;
    }

    snapshot->light_raw = light_raw_snapshot;
    snapshot->light_avg = light_avg_snapshot;
    snapshot->light_level =
        light_level_to_string(light_avg_snapshot);
    snapshot->led_state =
        device_status.led_on ? "on" : "off";

    snapshot->temperature_raw =
        temperature_raw_snapshot;
    snapshot->temperature_avg =
        temperature_avg_snapshot;
    snapshot->temperature_level =
        temperature_level_to_string(
            temperature_level_snapshot
        );
    snapshot->fan_state =
        device_status.fan_on ? "on" : "off";

    snapshot->uptime_ms = HAL_GetTick();

    snapshot->human_raw =
        human_snapshot.human_raw;
    snapshot->human_motion =
        human_snapshot.human_motion;
    snapshot->human_detected =
        human_snapshot.human_detected;
    snapshot->human_valid =
        human_snapshot.human_valid;

    return 1;
}

static int light_telemetry_upload(
    const uint8_t server_ip[4],
    uint16_t server_port,
    const char *host
);

static void temperature_sensor_publish_data(
    uint16_t raw,
    uint16_t avg,
    TemperatureLevel_t level
)
{
    taskENTER_CRITICAL();

    g_temperature_shared_data.raw = raw;
    g_temperature_shared_data.avg = avg;
    g_temperature_shared_data.level = level;
    g_temperature_shared_data.valid = 1U;

    taskEXIT_CRITICAL();
}

static int temperature_sensor_get_latest_data(
    uint16_t *raw,
    uint16_t *avg,
    TemperatureLevel_t *level
)
{
    int ready = 0;

    if (raw == NULL || avg == NULL || level == NULL)
    {
        return 0;
    }

    taskENTER_CRITICAL();

    if (g_temperature_shared_data.valid != 0U)
    {
        *raw = g_temperature_shared_data.raw;
        *avg = g_temperature_shared_data.avg;
        *level = g_temperature_shared_data.level;
        ready = 1;
    }

    taskEXIT_CRITICAL();

    return ready;
}

static void light_sensor_publish_data(uint16_t raw, uint16_t avg)
{
    taskENTER_CRITICAL();

    g_light_shared_data.raw = raw;
    g_light_shared_data.avg = avg;
    g_light_shared_data.valid = 1U;

    taskEXIT_CRITICAL();
}

static int light_sensor_get_latest_data(uint16_t *raw, uint16_t *avg)
{
    int ready = 0;

    if (raw == NULL || avg == NULL)
    {
        return 0;
    }

    taskENTER_CRITICAL();

    if (g_light_shared_data.valid != 0U)
    {
        *raw = g_light_shared_data.raw;
        *avg = g_light_shared_data.avg;
        ready = 1;
    }

    taskEXIT_CRITICAL();

    return ready;
}

static void human_sensor_publish_data(
    uint8_t human_raw,
    uint8_t human_motion,
    uint8_t human_detected,
    uint8_t human_valid
)
{
    taskENTER_CRITICAL();

    g_human_shared_data.human_raw =
        (human_raw != 0U) ? 1U : 0U;

    g_human_shared_data.human_motion =
        (human_motion != 0U) ? 1U : 0U;

    g_human_shared_data.human_detected =
        (human_detected != 0U) ? 1U : 0U;

    g_human_shared_data.human_valid =
        (human_valid != 0U) ? 1U : 0U;

    taskEXIT_CRITICAL();
}

int human_sensor_get_snapshot(HumanSensorSnapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return 0;
    }

    taskENTER_CRITICAL();

    snapshot->human_raw =
        g_human_shared_data.human_raw;

    snapshot->human_motion =
        g_human_shared_data.human_motion;

    snapshot->human_detected =
        g_human_shared_data.human_detected;

    snapshot->human_valid =
        g_human_shared_data.human_valid;

    taskEXIT_CRITICAL();

    return 1;
}

static int json_escape_string(
    const char *source,
    char *destination,
    uint32_t destination_size
)
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

        if (value == '"' || value == '\\')
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

static const char *light_level_to_string(uint16_t value)
{
    if (value >= LIGHT_DARK_THRESHOLD)
    {
        return "DARK";
    }

    if (value <= LIGHT_BRIGHT_THRESHOLD)
    {
        return "BRIGHT";
    }

    return "NORMAL";
}

static void light_led_update(uint16_t light_filtered)
{
    if (g_light_led_on == 0U)
    {
        if (light_filtered >= LIGHT_LED_ON_THRESHOLD)
        {
            HAL_GPIO_WritePin(
                LIGHT_LED_GPIO_Port,
                LIGHT_LED_Pin,
                LIGHT_LED_ON_LEVEL
            );

            g_light_led_on = 1U;

            printf(
                "[LIGHT CTRL] LED ON, avg=%u\r\n",
                (unsigned int)light_filtered
            );
        }
    }
    else
    {
        if (light_filtered <= LIGHT_LED_OFF_THRESHOLD)
        {
            HAL_GPIO_WritePin(
                LIGHT_LED_GPIO_Port,
                LIGHT_LED_Pin,
                LIGHT_LED_OFF_LEVEL
            );

            g_light_led_on = 0U;

            printf(
                "[LIGHT CTRL] LED OFF, avg=%u\r\n",
                (unsigned int)light_filtered
            );
        }
    }
}

static TemperatureLevel_t temperature_get_level(uint16_t temperature_avg)
{
    if (temperature_avg <= TEMP_HOT_THRESHOLD)
    {
        return TEMPERATURE_LEVEL_HOT;
    }

    if (temperature_avg >= TEMP_COLD_THRESHOLD)
    {
        return TEMPERATURE_LEVEL_COLD;
    }

    return TEMPERATURE_LEVEL_NORMAL;
}

static const char *temperature_level_to_string(TemperatureLevel_t level)
{
    switch (level)
    {
        case TEMPERATURE_LEVEL_COLD:
            return "COLD";

        case TEMPERATURE_LEVEL_NORMAL:
            return "NORMAL";

        case TEMPERATURE_LEVEL_HOT:
            return "HOT";

        default:
            return "INVALID";
    }
}

static uint8_t temperature_fan_control(uint16_t temperature_avg)
{
    DeviceStatus_t device_status = {0};
    uint8_t manual_on_lock;
    int ret;

    ret = device_tool_get_status(&device_status);

    if (ret != DEVICE_TOOL_OK)
    {
        printf(
            "[TEMP CTRL ERROR] get status failed, ret=%d\r\n",
            ret
        );

        return 0U;
    }

    manual_on_lock = device_tool_is_fan_manual_on_locked();

    /* 第一优先级：高温安全强制开启。 */
    if (temperature_avg <= TEMP_FAN_ON_THRESHOLD)
    {
        if (device_status.fan_on == 0U)
        {
            ret = device_tool_set_fan_auto(1U);

            if (ret == DEVICE_TOOL_OK)
            {
                printf(
                    "[TEMP CTRL] safety force fan ON, avg=%u\r\n",
                    (unsigned int)temperature_avg
                );
            }
            else
            {
                printf(
                    "[TEMP CTRL ERROR] auto fan ON failed, ret=%d\r\n",
                    ret
                );
            }
        }
    }
    /* 第二优先级：Agent 手动开启锁定，保持开启。 */
    else if (manual_on_lock != 0U)
    {
        if (device_status.fan_on == 0U)
        {
            ret = device_tool_set_fan_auto(1U);

            if (ret == DEVICE_TOOL_OK)
            {
                printf(
                    "[TEMP CTRL] manual lock keeps fan ON, avg=%u\r\n",
                    (unsigned int)temperature_avg
                );
            }
            else
            {
                printf(
                    "[TEMP CTRL ERROR] manual lock fan ON failed, ret=%d\r\n",
                    ret
                );
            }
        }
    }
    /* 第三优先级：未锁定且温度恢复，自动关闭。 */
    else if (temperature_avg >= TEMP_FAN_OFF_THRESHOLD)
    {
        if (device_status.fan_on != 0U)
        {
            ret = device_tool_set_fan_auto(0U);

            if (ret == DEVICE_TOOL_OK)
            {
                printf(
                    "[TEMP CTRL] fan OFF, avg=%u\r\n",
                    (unsigned int)temperature_avg
                );
            }
            else
            {
                printf(
                    "[TEMP CTRL ERROR] auto fan OFF failed, ret=%d\r\n",
                    ret
                );
            }
        }
    }
    else
    {
        /* 1801～1949：保持 PE7 当前状态。 */
    }

    ret = device_tool_get_status(&device_status);

    if (ret != DEVICE_TOOL_OK)
    {
        printf(
            "[TEMP CTRL ERROR] final status failed, ret=%d\r\n",
            ret
        );

        return 0U;
    }

    return device_status.fan_on;
}

void freertos_start(void)
{
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
    (void)pvParameters;

    taskENTER_CRITICAL();

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

    xTaskCreate(
        task_light_sensor,
        "task_light",
        TASK_LIGHT_STACK_DEPTH,
        NULL,
        TASK_LIGHT_PRIORITY,
        &light_task_handle
    );

    xTaskCreate(
        task_temperature_sensor,
        "task_temp",
        TASK_TEMPERATURE_STACK_DEPTH,
        NULL,
        TASK_TEMPERATURE_PRIORITY,
        &temperature_task_handle
    );
    

    xTaskCreate(
        task_human_sensor,
        "task_human",
        TASK_HUMAN_STACK_DEPTH,
        NULL,
        TASK_HUMAN_PRIORITY,
        &human_task_handle
    );

    taskEXIT_CRITICAL();
    vTaskDelete(NULL);
}

void task1(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelete(NULL);
}

void task2(void *pvParameters)
{
    (void)pvParameters;

    printf("UART monitor task ready\r\n");

    while (1)
    {
        if (uart_command_take_overflow() != 0U)
        {
            printf(
                "[UART ERROR] command too long, line discarded\r\n"
            );
        }

        if (uart_command_take_dropped() != 0U)
        {
            printf(
                "[UART ERROR] command dropped, previous command pending\r\n"
            );
        }

        if (uart_command_take_rx_error() != 0U)
        {
            printf(
                "[UART ERROR] receive error, receiver restarted\r\n"
            );
        }

        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

void task_light_sensor(void *pvParameters)
{
    uint16_t sample_buffer[LIGHT_FILTER_SIZE] = {0U};
    uint16_t light_raw = 0U;
    uint16_t light_filtered = 0U;
    uint32_t sample_sum = 0U;
    uint8_t sample_index = 0U;
    uint8_t sample_count = 0U;
    uint8_t print_count = 0U;
    int ret;

    (void)pvParameters;

    printf("[LIGHT] task start\r\n");

    ret = light_sensor_init();

    if (ret != LIGHT_SENSOR_OK)
    {
        printf("[LIGHT ERROR] init failed, ret=%d\r\n", ret);

        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }

    printf("[LIGHT] ADC calibration OK\r\n");

    while (1)
    {
        ret = light_sensor_read_raw(&light_raw);

        if (ret == LIGHT_SENSOR_OK)
        {
            if (sample_count < LIGHT_FILTER_SIZE)
            {
                sample_buffer[sample_index] = light_raw;
                sample_sum += light_raw;
                sample_count++;
                sample_index++;

                if (sample_index >= LIGHT_FILTER_SIZE)
                {
                    sample_index = 0U;
                }
            }
            else
            {
                sample_sum -= sample_buffer[sample_index];
                sample_buffer[sample_index] = light_raw;
                sample_sum += light_raw;
                sample_index++;

                if (sample_index >= LIGHT_FILTER_SIZE)
                {
                    sample_index = 0U;
                }
            }

            if (sample_count == LIGHT_FILTER_SIZE)
            {
                light_filtered =
                    (uint16_t)(sample_sum / LIGHT_FILTER_SIZE);

                light_sensor_publish_data(light_raw, light_filtered);
                light_led_update(light_filtered);

                print_count++;

                if (print_count >= 10U)
                {
                    print_count = 0U;

                    printf(
                        "[LIGHT] raw=%u, avg=%u, level=%s\r\n",
                        (unsigned int)light_raw,
                        (unsigned int)light_filtered,
                        light_level_to_string(light_filtered)
                    );
                }
            }
        }
        else
        {
            printf("[LIGHT ERROR] read failed, ret=%d\r\n", ret);
        }

        vTaskDelay(pdMS_TO_TICKS(LIGHT_SAMPLE_PERIOD_MS));
    }
}

static int light_telemetry_get_snapshot(
    LightTelemetrySnapshot_t *snapshot
)
{
    uint16_t light_raw_snapshot;
    uint16_t light_avg_snapshot;
    uint16_t temperature_raw_snapshot;
    uint16_t temperature_avg_snapshot;
    TemperatureLevel_t temperature_level_snapshot;
    DeviceStatus_t device_status = {0};
    int status_ret;

    if (snapshot == NULL)
    {
        return 0;
    }

    if (light_sensor_get_latest_data(
            &light_raw_snapshot,
            &light_avg_snapshot
        ) == 0)
    {
        return 0;
    }

    if (temperature_sensor_get_latest_data(
            &temperature_raw_snapshot,
            &temperature_avg_snapshot,
            &temperature_level_snapshot
        ) == 0)
    {
        return 0;
    }

    status_ret = device_tool_get_status(&device_status);

    if (status_ret != DEVICE_TOOL_OK)
    {
        return 0;
    }

    snapshot->light_raw = light_raw_snapshot;
    snapshot->light_avg = light_avg_snapshot;
    snapshot->light_level = light_level_to_string(light_avg_snapshot);
    snapshot->led_state = device_status.led_on ? "on" : "off";

    snapshot->temperature_raw = temperature_raw_snapshot;
    snapshot->temperature_avg = temperature_avg_snapshot;
    snapshot->temperature_level =
        temperature_level_to_string(temperature_level_snapshot);

    /* fan 状态来自 device_tool_get_status() 对 PE7 的实际读取。 */
    snapshot->fan_state = device_status.fan_on ? "on" : "off";
    snapshot->uptime_ms = HAL_GetTick();

    return 1;
}

static int light_telemetry_upload(
    const uint8_t server_ip[4],
    uint16_t server_port,
    const char *host
)
{
    static char telemetry_json[LIGHT_TELEMETRY_JSON_SIZE];
    static char telemetry_response[LIGHT_TELEMETRY_RESPONSE_SIZE];
    static uint32_t success_count = 0U;
    static uint32_t consecutive_fail_count = 0U;

    LightTelemetrySnapshot_t snapshot;
    int format_ret;
    int post_ret;

    if (server_ip == NULL || host == NULL)
    {
        return HTTP_CLIENT_ERR_PARAM;
    }

    if (!W5500_IsLinkUp())
    {
        return HTTP_CLIENT_ERR_CONNECT;
    }

    memset(&snapshot, 0, sizeof(snapshot));

    if (light_telemetry_get_snapshot(&snapshot) == 0)
    {
        return HTTP_CLIENT_ERR_PARAM;
    }

    if (snapshot.light_level == NULL ||
        snapshot.led_state == NULL ||
        snapshot.temperature_level == NULL ||
        snapshot.fan_state == NULL)
    {
        printf("[TEL][ERR] invalid sensor snapshot\r\n");
        return HTTP_CLIENT_ERR_PARAM;
    }

    format_ret = snprintf(
        telemetry_json,
        sizeof(telemetry_json),
        "{"
        "\"light_raw\":%u,"
        "\"light_avg\":%u,"
        "\"light_level\":\"%s\","
        "\"led\":\"%s\","

        "\"temperature_raw\":%u,"
        "\"temperature_avg\":%u,"
        "\"temperature_level\":\"%s\","
        "\"fan\":\"%s\","

        "\"uptime_ms\":%lu,"

        "\"human_raw\":%u,"
        "\"human_motion\":%u,"
        "\"human_detected\":%u,"
        "\"human_valid\":%u"
        "}",

        (unsigned int)snapshot.light_raw,
        (unsigned int)snapshot.light_avg,
        snapshot.light_level,
        snapshot.led_state,

        (unsigned int)snapshot.temperature_raw,
        (unsigned int)snapshot.temperature_avg,
        snapshot.temperature_level,
        snapshot.fan_state,

        (unsigned long)snapshot.uptime_ms,

        (unsigned int)snapshot.human_raw,
        (unsigned int)snapshot.human_motion,
        (unsigned int)snapshot.human_detected,
        (unsigned int)snapshot.human_valid
    );

    if (format_ret <= 0 ||
        format_ret >= (int)sizeof(telemetry_json))
    {
        printf(
            "[TEL][ERR] JSON truncated, required=%d, buffer=%d\r\n",
            format_ret,
            (int)sizeof(telemetry_json)
        );

        return HTTP_CLIENT_ERR_PARAM;
    }

    telemetry_response[0] = '\0';

    post_ret = http_post_timeout(
        server_ip,
        server_port,
        host,
        "/api/sensor_data",
        "application/json",
        telemetry_json,
        telemetry_response,
        sizeof(telemetry_response),
        LIGHT_TELEMETRY_RECV_TIMEOUT_MS
    );

    if (post_ret < 0)
    {
        consecutive_fail_count++;

        if (consecutive_fail_count == 1U ||
            (consecutive_fail_count % 5U) == 0U)
        {
            printf(
                "[TEL][ERR] upload failed, ret=%d, failures=%lu\r\n",
                post_ret,
                (unsigned long)consecutive_fail_count
            );
        }

        return post_ret;
    }

    consecutive_fail_count = 0U;
    success_count++;

    if (success_count == 1U || (success_count % 5U) == 0U)
    {
        printf(
            "[TEL] upload ok, "
            "light=%u/%u/%s, led=%s, "
            "temp=%u/%u/%s, fan=%s, "
            "human=%u/%u/%u, valid=%u, "
            "uptime=%lu\r\n",

            (unsigned int)snapshot.light_raw,
            (unsigned int)snapshot.light_avg,
            snapshot.light_level,
            snapshot.led_state,

            (unsigned int)snapshot.temperature_raw,
            (unsigned int)snapshot.temperature_avg,
            snapshot.temperature_level,
            snapshot.fan_state,

            (unsigned int)snapshot.human_raw,
            (unsigned int)snapshot.human_motion,
            (unsigned int)snapshot.human_detected,
            (unsigned int)snapshot.human_valid,

            (unsigned long)snapshot.uptime_ms
        );
    }

    return post_ret;
}

void task_temperature_sensor(void *pvParameters)
{
    uint16_t sample_buffer[TEMP_FILTER_SIZE] = {0U};
    uint16_t temperature_raw = 0U;
    uint16_t temperature_avg = 0U;
    uint32_t sample_sum = 0U;
    uint8_t sample_index = 0U;
    uint8_t sample_count = 0U;
    uint8_t print_count = 0U;
    uint8_t fan_on = 0U;
    TemperatureLevel_t temperature_level;
    TickType_t last_wake_time;
    int ret;

    (void)pvParameters;

    printf("[TEMP] task start\r\n");

    ret = temperature_sensor_init();

    if (ret != TEMPERATURE_SENSOR_OK)
    {
        printf("[TEMP ERROR] init failed, ret=%d\r\n", ret);

        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }

    printf("[TEMP] ADC3 calibration OK\r\n");

    last_wake_time = xTaskGetTickCount();

    while (1)
    {
        ret = temperature_sensor_read_raw(&temperature_raw);

        if (ret != TEMPERATURE_SENSOR_OK)
        {
            printf("[TEMP ERROR] read failed, ret=%d\r\n", ret);
        }
        else if ((temperature_raw < TEMP_ADC_VALID_MIN) ||
                 (temperature_raw > TEMP_ADC_VALID_MAX))
        {
            printf(
                "[TEMP ERROR] invalid raw=%u\r\n",
                (unsigned int)temperature_raw
            );
        }
        else
        {
            if (sample_count < TEMP_FILTER_SIZE)
            {
                sample_buffer[sample_index] = temperature_raw;
                sample_sum += temperature_raw;
                sample_count++;
                sample_index++;

                if (sample_index >= TEMP_FILTER_SIZE)
                {
                    sample_index = 0U;
                }
            }
            else
            {
                sample_sum -= sample_buffer[sample_index];
                sample_buffer[sample_index] = temperature_raw;
                sample_sum += temperature_raw;
                sample_index++;

                if (sample_index >= TEMP_FILTER_SIZE)
                {
                    sample_index = 0U;
                }
            }

            if (sample_count == TEMP_FILTER_SIZE)
            {
                temperature_avg =
                    (uint16_t)(sample_sum / TEMP_FILTER_SIZE);

                temperature_level =
                    temperature_get_level(temperature_avg);

                fan_on = temperature_fan_control(temperature_avg);

                temperature_sensor_publish_data(
                    temperature_raw,
                    temperature_avg,
                    temperature_level
                );

                print_count++;

                if (print_count >= 4U)
                {
                    print_count = 0U;

                    printf(
                        "[TEMP] raw=%u, avg=%u, level=%s, fan=%s\r\n",
                        (unsigned int)temperature_raw,
                        (unsigned int)temperature_avg,
                        temperature_level_to_string(temperature_level),
                        fan_on ? "ON" : "OFF"
                    );
                }
            }
        }

        vTaskDelayUntil(
            &last_wake_time,
            pdMS_TO_TICKS(TEMP_SAMPLE_PERIOD_MS)
        );
    }
}

void task_w5500(void *pvParameters)
{
    uint8_t version;
    uint8_t server_ip[4] = {192U, 168U, 1U, 100U};

    static char uart_command[UART_COMMAND_BUFFER_SIZE];
    static char escaped_command[UART_JSON_ESCAPED_BUFFER_SIZE];
    static char request_body[UART_JSON_REQUEST_BUFFER_SIZE];
    static char response[1024];
    static char report_response[512];
    static char result_body[320];
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

    TickType_t next_telemetry_tick;
    TickType_t current_tick;

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
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }

    printf("W5500 SPI OK\r\n");

    W5500_NetworkConfig();
    W5500_PrintNetworkInfo();

    while (!W5500_IsLinkUp())
    {
        printf("W5500 Link DOWN\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }

    printf("W5500 Link UP\r\n");
    printf("[UART READY] enter command and press Enter\r\n");

    next_telemetry_tick =
        xTaskGetTickCount() +
        pdMS_TO_TICKS(LIGHT_TELEMETRY_PERIOD_MS);

    while (1)
    {
        if (!W5500_IsLinkUp())
        {
            printf("[NET][ERROR] W5500 Link DOWN\r\n");
            vTaskDelay(pdMS_TO_TICKS(1000U));
            continue;
        }

        if (uart_command_is_ready() == 0U)
        {
            current_tick = xTaskGetTickCount();

            if ((int32_t)(current_tick - next_telemetry_tick) >= 0)
            {
                next_telemetry_tick =
                    current_tick +
                    pdMS_TO_TICKS(LIGHT_TELEMETRY_PERIOD_MS);

                ret = light_telemetry_upload(
                    server_ip,
                    8080U,
                    "192.168.1.100"
                );

                (void)ret;
            }

            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }

        command_length = uart_command_get(
            uart_command,
            (uint16_t)sizeof(uart_command)
        );

        if (command_length == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }

        if (command_length < 0)
        {
            printf(
                "[UART ERROR] command get failed, ret=%d\r\n",
                command_length
            );

            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }

        printf("\r\n");
        printf("================================\r\n");
        printf("[UART CMD] %s\r\n", uart_command);
        printf("[UART CMD] byte length=%d\r\n", command_length);
        printf("================================\r\n");

        escape_ret = json_escape_string(
            uart_command,
            escaped_command,
            sizeof(escaped_command)
        );

        if (escape_ret < 0)
        {
            printf(
                "[JSON ERROR] command escape failed, ret=%d\r\n",
                escape_ret
            );

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
            request_format_ret >= (int)sizeof(request_body))
        {
            printf(
                "[JSON ERROR] request body build failed, required=%d, buffer=%d\r\n",
                request_format_ret,
                (int)sizeof(request_body)
            );

            printf("[UART READY] enter next command\r\n");
            continue;
        }

        printf("[HTTP] POST /api/test\r\n");
        printf("[HTTP] request body: %s\r\n", request_body);

        response[0] = '\0';

        ret = http_post(
            server_ip,
            8080U,
            "192.168.1.100",
            "/api/test",
            "application/json",
            request_body,
            response,
            sizeof(response)
        );

        if (ret < 0)
        {
            printf("[HTTP ERROR] /api/test failed, ret=%d\r\n", ret);
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

        ret = json_get_string(
            body_ptr,
            "action",
            action,
            sizeof(action)
        );

        if (ret != SIMPLE_JSON_OK)
        {
            printf("[JSON ERROR] action parse failed, ret=%d\r\n", ret);
            printf("[UART READY] enter next command\r\n");
            continue;
        }

        printf("JSON action = %s\r\n", action);

        ret = json_get_int(body_ptr, "times", &times);

        if (ret == SIMPLE_JSON_OK)
        {
            printf("JSON times = %d\r\n", times);
        }
        else if (strcmp(action, "led_blink") == 0)
        {
            printf("JSON times missing, led_blink uses default 1\r\n");
            times = 1;
        }
        else
        {
            times = 0;
        }

        ret = json_get_string(body_ptr, "msg", msg, sizeof(msg));

        if (ret == SIMPLE_JSON_OK)
        {
            printf("JSON msg = %s\r\n", msg);
        }
        else
        {
            printf("JSON msg parse failed, ret=%d\r\n", ret);
        }

        tool_ret = device_tool_execute(action, times);

        include_device_status = 0;
        status_ret = DEVICE_TOOL_OK;
        memset(&device_status, 0, sizeof(device_status));

        if ((strcmp(action, "get_device_status") == 0) &&
            (tool_ret == DEVICE_TOOL_OK))
        {
            status_ret = device_tool_get_status(&device_status);

            if (status_ret == DEVICE_TOOL_OK)
            {
                include_device_status = 1;
            }
            else
            {
                tool_ret = status_ret;
                printf(
                    "Device status read failed, ret=%d\r\n",
                    status_ret
                );
            }
        }

        switch (tool_ret)
        {
            case DEVICE_TOOL_OK:
                result_msg =
                    (strcmp(action, "get_device_status") == 0) ?
                    "device status queried" :
                    "tool executed on stm32";
                break;

            case DEVICE_TOOL_ERR_PARAM:
                result_msg = "invalid tool parameter";
                break;

            case DEVICE_TOOL_ERR_UNKNOWN:
                result_msg = "unknown tool action";
                break;

            case DEVICE_TOOL_ERR_RANGE:
                result_msg = "tool parameter out of range";
                break;

            case DEVICE_TOOL_ERR_NOT_READY:
                result_msg = "tool hardware not ready";
                break;

            default:
                result_msg = "tool execution failed";
                break;
        }

        if (tool_ret == DEVICE_TOOL_OK)
        {
            result_status = "ok";
            printf("Tool execute OK\r\n");
        }
        else
        {
            result_status =
                (strcmp(action, "get_device_status") == 0) ?
                "error" : "failed";

            printf("Tool execute failed, ret=%d\r\n", tool_ret);
        }

        if (include_device_status != 0)
        {
            led_state = device_status.led_on ? "on" : "off";
            fan_state = device_status.fan_on ? "on" : "off";
            buzzer_state = device_status.buzzer_on ? "on" : "off";

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
            result_format_ret >= (int)sizeof(result_body))
        {
            printf(
                "[JSON ERROR] tool result build failed, required=%d, buffer=%d\r\n",
                result_format_ret,
                (int)sizeof(result_body)
            );

            printf("[UART READY] enter next command\r\n");
            continue;
        }

        printf("Report tool result:\r\n");
        printf("%s\r\n", result_body);

        report_response[0] = '\0';

        ret = http_post(
            server_ip,
            8080U,
            "192.168.1.100",
            "/api/tool_result",
            "application/json",
            result_body,
            report_response,
            sizeof(report_response)
        );

        if (ret >= 0)
        {
            printf("Tool result report response:\r\n");
            printf("%s\r\n", report_response);
        }
        else
        {
            printf(
                "[HTTP ERROR] tool result report failed, ret=%d\r\n",
                ret
            );
        }

        printf("[UART READY] enter next command\r\n");
    }
}

void task_human_sensor(void *pvParameters)
{
    TickType_t last_wake_time;
    TickType_t now_tick;
    TickType_t last_motion_tick = 0U;
    const TickType_t human_hold_ticks =
        pdMS_TO_TICKS(HUMAN_HOLD_TIME_MS);

    uint8_t human_raw = 0U;
    uint8_t last_raw = 0xFFU;

    /*
     * stable_motion:
     * 0 = PIR稳定低电平，当前无活动
     * 1 = PIR稳定高电平，当前检测到活动
     * 0xFF = 尚未完成稳定判定
     */
    uint8_t stable_motion = 0xFFU;

    /*
     * human_detected不是严格的静态人体存在结果，
     * 而是“最近一段时间检测到人体活动”的推断状态。
     */
    uint8_t human_detected = 0U;

    uint8_t stable_count = 0U;
    uint8_t log_count = 0U;

    int ret;

    (void)pvParameters;

    printf("[HUMAN] task start\r\n");

    ret = human_sensor_init();

    if (ret != HUMAN_SENSOR_OK)
    {
        printf(
            "[HUMAN ERROR] init failed, ret=%d\r\n",
            ret
        );

        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U));
        }
    }

    /*
     * 预热期间共享状态无效。
     * 四个字段先发布为安全默认值，供其他任务读取。
     */
    human_sensor_publish_data(
        0U,
        0U,
        0U,
        0U
    );

    /*
     * PIR上电预热。
     * 这里只挂起人体任务，不影响光照、温度和网络任务。
     */
    printf(
        "[HUMAN] warming up, wait %u ms\r\n",
        (unsigned int)HUMAN_WARMUP_TIME_MS
    );

    vTaskDelay(pdMS_TO_TICKS(HUMAN_WARMUP_TIME_MS));

    printf("[HUMAN] sensor ready\r\n");

    last_wake_time = xTaskGetTickCount();

    while (1)
    {
        ret = human_sensor_read_raw(&human_raw);

        if (ret != HUMAN_SENSOR_OK)
        {
            /*
             * 读取失败时保留上一次 raw、motion 和 detected，
             * 但把 human_valid 清零，避免其他任务把旧数据
             * 当作当前有效状态。
             */
            human_sensor_publish_data(
                human_raw,
                (stable_motion == 1U) ? 1U : 0U,
                human_detected,
                0U
            );

            printf(
                "[HUMAN ERROR] read failed, ret=%d\r\n",
                ret
            );
        }
        else
        {
            /*
             * 连续多次相同才确认PIR原始状态，
             * 避免单次毛刺造成状态变化。
             */
            if (human_raw == last_raw)
            {
                if (stable_count < HUMAN_STABLE_SAMPLE_COUNT)
                {
                    stable_count++;
                }
            }
            else
            {
                last_raw = human_raw;
                stable_count = 1U;
            }

            /*
             * PIR稳定状态发生变化。
             */
            if ((stable_count >= HUMAN_STABLE_SAMPLE_COUNT) &&
                (stable_motion != human_raw))
            {
                stable_motion = human_raw;

                if (stable_motion != 0U)
                {
                    printf(
                        "[HUMAN EVENT] motion=MOTION\r\n"
                    );
                }
                else
                {
                    printf(
                        "[HUMAN EVENT] motion=NO_MOTION\r\n"
                    );
                }
            }

            now_tick = xTaskGetTickCount();

            /*
             * PIR当前稳定为高电平时，不断刷新最后活动时间。
             *
             * 这样即使模块因重复触发而持续保持高电平，
             * 30秒保持时间也从最后一个高电平采样开始计算。
             */
            if (stable_motion == 1U)
            {
                last_motion_tick = now_tick;

                if (human_detected == 0U)
                {
                    human_detected = 1U;

                    printf(
                        "[HUMAN STATE] detected=1, "
                        "state=PRESENT_INFERRED\r\n"
                    );
                }
            }
            else if ((stable_motion == 0U) &&
                     (human_detected != 0U))
            {
                /*
                 * PIR已经恢复低电平，但不立即判定无人。
                 * 只有超过保持时间后才切换状态。
                 *
                 * TickType_t无符号减法也能正确处理Tick溢出。
                 */
                if ((TickType_t)(now_tick - last_motion_tick) >=
                    human_hold_ticks)
                {
                    human_detected = 0U;

                    printf(
                        "[HUMAN STATE] detected=0, "
                        "state=ABSENT_INFERRED, "
                        "timeout=%u ms\r\n",
                        (unsigned int)HUMAN_HOLD_TIME_MS
                    );
                }
            }
            else
            {
                /*
                 * 尚未完成稳定判断，或者状态无需改变。
                 */
            }

            /*
             * 一次性发布四个人体字段。
             *
             * human_raw：
             *     当前PE9原始数字输入，归一化为0/1。
             *
             * human_motion：
             *     连续HUMAN_STABLE_SAMPLE_COUNT次一致采样后
             *     得到的稳定活动状态。
             *
             * human_detected：
             *     带HUMAN_HOLD_TIME_MS保持时间的有人推断状态。
             *
             * human_valid：
             *     已完成预热、当前读取成功，并且已经形成
             *     稳定motion判定时为1。
             */
            human_sensor_publish_data(
                human_raw,
                (stable_motion == 1U) ? 1U : 0U,
                human_detected,
                (stable_motion != 0xFFU) ? 1U : 0U
            );

            /*
             * 每约2秒打印一次完整状态。
             */
            log_count++;

            if (log_count >= HUMAN_LOG_PERIOD_COUNT)
            {
                log_count = 0U;

                printf(
                    "[HUMAN] raw=%u, motion=%s, "
                    "detected=%u, state=%s\r\n",
                    (unsigned int)human_raw,

                    (stable_motion == 1U) ?
                        "MOTION" :
                    (stable_motion == 0U) ?
                        "NO_MOTION" :
                        "UNKNOWN",

                    (unsigned int)human_detected,

                    (human_detected != 0U) ?
                        "PRESENT_INFERRED" :
                        "ABSENT_INFERRED"
                );
            }
        }

        vTaskDelayUntil(
            &last_wake_time,
            pdMS_TO_TICKS(HUMAN_SAMPLE_PERIOD_MS)
        );
    }
}



void vApplicationGetIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer,
    StackType_t **ppxIdleTaskStackBuffer,
    uint32_t *pulIdleTaskStackSize
)
{
    static StaticTask_t idle_task_tcb;
    static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(
    StaticTask_t **ppxTimerTaskTCBBuffer,
    StackType_t **ppxTimerTaskStackBuffer,
    uint32_t *pulTimerTaskStackSize
)
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

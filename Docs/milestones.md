# 阶段版本记录

本项目保留实际开发过程中的阶段提交，并使用 Git Tag 固定可运行里程碑。

| Tag | 日期 | 关键提交 | 验收内容 |
| --- | --- | --- | --- |
| `v0.1.0-freertos` | 2026-06-30 | `32b7eb9` | 工程分层、FreeRTOS 调度、LED 与串口最小验证 |
| `v0.2.0-w5500` | 2026-07-01 | `c006e97` | W5500 版本读取、链路、Ping、TCP 与 HTTP GET |
| `v0.3.0-local-agent` | 2026-07-07 | `05177e7` | Python 本地服务、JSON 协议和设备动作映射 |
| `v0.4.0-cloud-agent` | 2026-07-09 | `bbf8581` | DeepSeek API 接入和结构化动作返回 |
| `v0.5.0-agent-loop` | 2026-07-14 | `54db0c2` | 串口命令、模型决策、设备执行和结果回传闭环 |
| `v0.6.0-sensors` | 当前 | `5abf5c2`、`7b27490` | 光照、温度、人体采集与 Agent 可信上下文 |

## 版本说明

### v0.1.0-freertos

- 将应用代码与 CubeMX 生成代码分层。
- 手动移植 FreeRTOS Cortex-M7 端口和 `heap_4`。
- 完成任务调度、LED 闪烁和串口输出。

### v0.2.0-w5500

- 通过 SPI1 驱动 W5500。
- 完成硬复位、版本寄存器读取和静态网络配置。
- 使用 Socket 0 完成 TCP 连接和 HTTP GET 最小验证。

### v0.3.0-local-agent

- 增加 HTTP 客户端、简单 JSON 解析和设备工具层。
- 使用本地 Python 服务模拟 Agent 决策。
- 验证命令发送、动作返回和设备执行链路。

### v0.4.0-cloud-agent

- Python 代理通过环境变量读取 DeepSeek API Key。
- 将用户命令发送至云端模型，并规范化模型返回 JSON。
- API Key 不进入源码和 Git 历史。

### v0.5.0-agent-loop

- USART1 中断接收用户命令。
- STM32 调用 `/api/test` 获取动作并通过 `device_tool` 执行。
- 通过 `/api/tool_result` 回传真实执行结果。

### v0.6.0-sensors

- 增加 ADC1 光照和 ADC3 温度采样 BSP。
- 增加人体感应输入、稳定判断和保持时间。
- 增加光照 LED 与温度风扇自动控制。
- 周期上报传感器状态，并作为 DeepSeek 的可信实时上下文。
- 本地服务增加数据校验、过期判断和浏览器状态面板。

## 后续版本建议

- `v0.7.0-calibration`：传感器标定和配置集中管理。
- `v0.8.0-resilience`：网络自动重连、错误统计和任务监控。
- `v1.0.0-demo`：完整硬件演示、接线图、测试记录和发布包。

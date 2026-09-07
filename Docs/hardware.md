# 硬件连接

## 核心平台

| 项目 | 当前配置 |
| --- | --- |
| MCU | STM32H743ZIT6，LQFP144 |
| 系统时钟 | 400 MHz |
| HCLK | 200 MHz |
| APB1/APB2/APB3/APB4 | 100 MHz |
| 调试接口 | SWD，PA13/PA14 保留 |

## W5500

| W5500 信号 | STM32 引脚 | 配置 |
| --- | --- | --- |
| SCK | PA5 | SPI1 SCK |
| MISO | PA6 | SPI1 MISO |
| MOSI | PA7 | SPI1 MOSI |
| CS | PB6 | GPIO 输出，默认高 |
| RST | PB7 | GPIO 输出，默认高 |
| INT | PB5 | GPIO 输入，上拉，当前轮询 |
| GND | GND | 必须共地 |
| VCC | 3.3 V | 按模块规格确认供电 |

SPI1 当前配置为全双工主机、Mode 0、8-bit、MSB First、Software NSS，
预分频 32，时钟约 6.25 Mbit/s。

## 串口

当前 CubeMX 配置使用 USART1 的 PA9/PA10 引脚：

| STM32 | USB-TTL |
| --- | --- |
| PA9 / USART1_TX | RX |
| PA10 / USART1_RX | TX |
| GND | GND |

串口参数为 `115200 8N1`，无硬件流控。USB-TTL 使用 3.3 V 逻辑电平。

## 传感器与执行器

| 模块 | STM32 引脚 | 当前用途 |
| --- | --- | --- |
| 光照传感器模拟量 | PA3 / ADC1_INP15 | 12 位 ADC 采样 |
| 温度传感器模拟量 | PF3 / ADC3_INP5 | 12 位 ADC 采样 |
| 人体感应数字量 | PE9 | GPIO 输入，高电平有效 |
| 光照联动 LED | PE13 | GPIO 输出 |
| 风扇控制 | PE7 | GPIO 输出，高电平开启 |
| 蜂鸣器控制 | PE8 | GPIO 输出，高电平开启 |
| 基础状态 LED | PG7 | GPIO 输出 |

传感器模拟输出必须处于 STM32 ADC 允许的电压范围内。风扇、蜂鸣器等负载不得直接由 GPIO 大电流驱动，
实际硬件应使用合适的三极管、MOSFET、续流二极管和独立供电，并保证系统共地。

## 当前网络参数

| 参数 | 值 |
| --- | --- |
| W5500 IP | `192.168.1.123` |
| 子网掩码 | `255.255.255.0` |
| 网关 | `192.168.1.1` |
| Agent 服务 | `192.168.1.100:8080` |

这些参数目前写在源码中，连接前需要确保电脑网卡与 W5500 位于同一网段。

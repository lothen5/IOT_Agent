/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    usart.c
 * @brief   This file provides code for the configuration
 *          of the USART instances.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "usart.h"

/* USER CODE BEGIN 0 */
#include <string.h>

/*
 * USART1 receives one raw byte at a time.
 * UTF-8 text is kept as-is; no character conversion is performed.
 *
 * Two line buffers are used so the interrupt callback can publish a completed
 * command by swapping buffer indexes instead of copying up to 256 bytes in ISR
 * context.
 */
static uint8_t g_uart_rx_byte;
static char g_uart_line_buffer[2][UART_COMMAND_BUFFER_SIZE];

static volatile uint8_t g_uart_fill_buffer_index;
static volatile uint8_t g_uart_ready_buffer_index;
static volatile uint16_t g_uart_fill_length;
static volatile uint16_t g_uart_ready_length;

static volatile uint8_t g_uart_command_ready;
static volatile uint8_t g_uart_discard_until_eol;
static volatile uint8_t g_uart_overflow_pending;
static volatile uint8_t g_uart_dropped_pending;
static volatile uint8_t g_uart_rx_error_pending;

static uint32_t uart_enter_critical(void)
{
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  __DMB();

  return primask;
}

static void uart_exit_critical(uint32_t primask)
{
  __DMB();
  __set_PRIMASK(primask);
}
/* USER CODE END 0 */

UART_HandleTypeDef huart1;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    PeriphClkInitStruct.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */
    /*
     * Priority 6 is below configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5).
     * The current callback does not call FreeRTOS APIs, but this priority also
     * leaves room for a later FromISR notification/queue implementation.
     */
    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9|GPIO_PIN_10);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE END USART1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
HAL_StatusTypeDef uart_command_receive_start(void)
{
  uint32_t primask;

  primask = uart_enter_critical();

  g_uart_fill_buffer_index = 0U;
  g_uart_ready_buffer_index = 0U;
  g_uart_fill_length = 0U;
  g_uart_ready_length = 0U;

  g_uart_command_ready = 0U;
  g_uart_discard_until_eol = 0U;
  g_uart_overflow_pending = 0U;
  g_uart_dropped_pending = 0U;
  g_uart_rx_error_pending = 0U;

  g_uart_line_buffer[0][0] = '\0';
  g_uart_line_buffer[1][0] = '\0';

  uart_exit_critical(primask);

  /* Arms one-byte interrupt reception; USART1 is not reinitialized here. */
  return HAL_UART_Receive_IT(&huart1, &g_uart_rx_byte, 1U);
}

uint8_t uart_command_is_ready(void)
{
  return g_uart_command_ready;
}

int uart_command_get(char *out_buffer, uint16_t out_buffer_size)
{
  uint32_t primask;
  uint16_t command_length;
  uint8_t ready_index;

  if ((out_buffer == NULL) || (out_buffer_size == 0U))
  {
    return UART_COMMAND_GET_INVALID_ARG;
  }

  primask = uart_enter_critical();

  if (g_uart_command_ready == 0U)
  {
    uart_exit_critical(primask);
    return 0;
  }

  command_length = g_uart_ready_length;
  ready_index = g_uart_ready_buffer_index;

  if (((uint32_t)command_length + 1U) > (uint32_t)out_buffer_size)
  {
    uart_exit_critical(primask);
    return UART_COMMAND_GET_BUFFER_SMALL;
  }

  memcpy(out_buffer,
         g_uart_line_buffer[ready_index],
         command_length);
  out_buffer[command_length] = '\0';

  g_uart_command_ready = 0U;
  g_uart_ready_length = 0U;

  uart_exit_critical(primask);

  return (int)command_length;
}

uint8_t uart_command_take_overflow(void)
{
  uint32_t primask;
  uint8_t pending;

  primask = uart_enter_critical();
  pending = g_uart_overflow_pending;
  g_uart_overflow_pending = 0U;
  uart_exit_critical(primask);

  return pending;
}

uint8_t uart_command_take_dropped(void)
{
  uint32_t primask;
  uint8_t pending;

  primask = uart_enter_critical();
  pending = g_uart_dropped_pending;
  g_uart_dropped_pending = 0U;
  uart_exit_critical(primask);

  return pending;
}

uint8_t uart_command_take_rx_error(void)
{
  uint32_t primask;
  uint8_t pending;

  primask = uart_enter_critical();
  pending = g_uart_rx_error_pending;
  g_uart_rx_error_pending = 0U;
  uart_exit_critical(primask);

  return pending;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  uint8_t received_byte;
  uint8_t fill_index;
  uint16_t fill_length;

  if (huart->Instance != USART1)
  {
    return;
  }

  received_byte = g_uart_rx_byte;
  fill_index = g_uart_fill_buffer_index;
  fill_length = g_uart_fill_length;

  if (g_uart_discard_until_eol != 0U)
  {
    if ((received_byte == '\r') || (received_byte == '\n'))
    {
      g_uart_discard_until_eol = 0U;
      g_uart_fill_length = 0U;
    }
  }
  else if ((received_byte == '\r') || (received_byte == '\n'))
  {
    /* Empty lines and the LF part of CRLF are ignored. */
    if (fill_length > 0U)
    {
      g_uart_line_buffer[fill_index][fill_length] = '\0';

      if (g_uart_command_ready == 0U)
      {
        /* Publish without copying a full line in interrupt context. */
        g_uart_ready_buffer_index = fill_index;
        g_uart_ready_length = fill_length;
        g_uart_command_ready = 1U;

        g_uart_fill_buffer_index = (uint8_t)(fill_index ^ 1U);
      }
      else
      {
        /* A previous complete command has not been consumed yet. */
        g_uart_dropped_pending = 1U;
      }

      g_uart_fill_length = 0U;
    }
  }
  else
  {
    if (fill_length < (UART_COMMAND_BUFFER_SIZE - 1U))
    {
      g_uart_line_buffer[fill_index][fill_length] = (char)received_byte;
      g_uart_fill_length = fill_length + 1U;
    }
    else
    {
      /* Discard this entire overlong line until CR or LF is received. */
      g_uart_fill_length = 0U;
      g_uart_discard_until_eol = 1U;
      g_uart_overflow_pending = 1U;
    }
  }

  /* Continue receiving the next raw byte. */
  if (HAL_UART_Receive_IT(&huart1, &g_uart_rx_byte, 1U) != HAL_OK)
  {
    g_uart_rx_error_pending = 1U;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART1)
  {
    return;
  }

  g_uart_fill_length = 0U;
  g_uart_discard_until_eol = 0U;
  g_uart_rx_error_pending = 1U;

  /* HAL has ended the failed RX transfer before this callback for fatal RX errors. */
  (void)HAL_UART_Receive_IT(&huart1, &g_uart_rx_byte, 1U);
}
/* USER CODE END 1 */

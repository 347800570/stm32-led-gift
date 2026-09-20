/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "usb.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define WS2812_LED_COUNT          10U
#define WS2812_BITS_PER_LED       24U
#define WS2812_RESET_SLOTS        64U
#define WS2812_BUFFER_SIZE        ((WS2812_LED_COUNT * WS2812_BITS_PER_LED) + WS2812_RESET_SLOTS)

/* TIM3 runs at 48 MHz with ARR=59: 19 ticks encode 0, 38 ticks encode 1. */
#define WS2812_DUTY_0             19U
#define WS2812_DUTY_1             38U
#define WS2812_DMA_TIMEOUT_MS     100U
#define WS2812_TEST_BRIGHTNESS    16U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint16_t ws2812_pwm_buffer[WS2812_BUFFER_SIZE];
static volatile uint8_t ws2812_dma_complete;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void WS2812_FillSolidColor(uint8_t red, uint8_t green, uint8_t blue);
static HAL_StatusTypeDef WS2812_Send(uint32_t channel);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief Convert one solid RGB color into a WS2812B PWM duty-cycle stream.
  * @note  WS2812B expects each pixel in GRB order, most-significant bit first.
  */
static void WS2812_FillSolidColor(uint8_t red, uint8_t green, uint8_t blue)
{
  uint32_t color = ((uint32_t)green << 16U) |
                   ((uint32_t)red << 8U) |
                   (uint32_t)blue;
  uint32_t index = 0U;

  for (uint32_t led = 0U; led < WS2812_LED_COUNT; led++)
  {
    for (uint32_t bit = 0U; bit < WS2812_BITS_PER_LED; bit++)
    {
      uint32_t mask = 1UL << (23U - bit);
      ws2812_pwm_buffer[index++] = ((color & mask) != 0U) ?
                                    WS2812_DUTY_1 : WS2812_DUTY_0;
    }
  }

  /* A low level longer than 50 us makes the LEDs latch the received color. */
  while (index < WS2812_BUFFER_SIZE)
  {
    ws2812_pwm_buffer[index++] = 0U;
  }
}

/**
  * @brief Send the prepared PWM stream through one TIM3 channel.
  */
static HAL_StatusTypeDef WS2812_Send(uint32_t channel)
{
  HAL_StatusTypeDef status;
  uint32_t start_tick;

  ws2812_dma_complete = 0U;
  __HAL_TIM_SET_COMPARE(&htim3, channel, 0U);
  __HAL_TIM_SET_COUNTER(&htim3, 0U);

  status = HAL_TIM_PWM_Start_DMA(&htim3,
                                 channel,
                                 (const uint32_t *)ws2812_pwm_buffer,
                                 (uint16_t)WS2812_BUFFER_SIZE);
  if (status != HAL_OK)
  {
    return status;
  }

  start_tick = HAL_GetTick();
  while (ws2812_dma_complete == 0U)
  {
    if ((HAL_GetTick() - start_tick) >= WS2812_DMA_TIMEOUT_MS)
    {
      (void)HAL_TIM_PWM_Stop_DMA(&htim3, channel);
      __HAL_TIM_SET_COMPARE(&htim3, channel, 0U);
      return HAL_TIMEOUT;
    }
  }

  (void)HAL_TIM_PWM_Stop_DMA(&htim3, channel);
  __HAL_TIM_SET_COMPARE(&htim3, channel, 0U);
  return HAL_OK;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_USB_PCD_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  /* Allow the 5 V LED supply and the level shifter to become stable. */
  HAL_Delay(100U);

  /* LED1_DATA on PB0/TIM3_CH3: ten low-brightness red pixels. */
  WS2812_FillSolidColor(WS2812_TEST_BRIGHTNESS, 0U, 0U);
  if (WS2812_Send(TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }

  /* LED2_DATA on PB1/TIM3_CH4: ten low-brightness green pixels. */
  WS2812_FillSolidColor(0U, WS2812_TEST_BRIGHTNESS, 0U);
  if (WS2812_Send(TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI48;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB|RCC_PERIPHCLK_USART1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/**
  * @brief Called by the HAL after one TIM PWM DMA transfer finishes.
  */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM3)
  {
    ws2812_dma_complete = 1U;
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

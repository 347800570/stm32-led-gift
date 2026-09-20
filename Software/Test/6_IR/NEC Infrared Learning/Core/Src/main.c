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
#include "adc.h"
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

#define IR_CAPTURE_MAX_EDGES       70U
#define IR_FRAME_GAP_MS            15U
//一帧NEC数据：起始码 → 地址 → 命令 → 校验 → 结束
//起始码：低电平约9 ms + 高电平约4.5 ms
//逻辑0：____低____|____高____
//        560 μs      560 μs

//逻辑1：____低____|____________高____________
//        560 μs             1690 μs
#define NEC_LEADER_LOW_MIN_US      8000U					//起始低电平：8~10 ms
#define NEC_LEADER_LOW_MAX_US      10000U					
#define NEC_LEADER_HIGH_MIN_US     3500U					//起始高电平：3.5~5.5 ms
#define NEC_LEADER_HIGH_MAX_US     5500U					
#define NEC_BIT_LOW_MIN_US         350U						//数据位低电平：350~800 us
#define NEC_BIT_LOW_MAX_US         800U
#define NEC_ZERO_HIGH_MIN_US       350U						//逻辑 0 高电平：350~900 us
#define NEC_ZERO_HIGH_MAX_US       900U
#define NEC_ONE_HIGH_MIN_US        1200U					//逻辑 1 高电平：1200~2100 us
#define NEC_ONE_HIGH_MAX_US        2100U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* EXTI 中断记录的原始红外脉冲。 */
static volatile uint16_t ir_duration_us[IR_CAPTURE_MAX_EDGES];		//原始波形
static volatile uint8_t ir_level[IR_CAPTURE_MAX_EDGES];						//原始波形
static volatile uint16_t ir_edge_count;														//已经记录多少段波形
static volatile uint8_t ir_capturing;															//是否正在接收一帧
static volatile uint8_t ir_frame_ready;														//是否已经收到完整帧
static volatile uint8_t ir_capture_overflow;											//数组是否装满
static volatile uint16_t ir_last_timer;														//上一次边沿的定时器值
static volatile uint32_t ir_last_edge_ms;													//最后一次边沿出现的系统时间

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

static void IR_ProcessFrame(void);
static uint8_t IR_InRange(uint16_t value, uint16_t min, uint16_t max);
static void IR_SendString(const char *text);
static void IR_SendHex8(uint8_t value);
static void IR_SendHex16(uint16_t value);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_TIM14_Init();
  MX_ADC_Init();
  /* USER CODE BEGIN 2 */

  /* TIM14 配置为 1 MHz，1 个计数约等于 1 us。 */
  if (HAL_TIM_Base_Start(&htim14) != HAL_OK)
  {
    Error_Handler();
  }

  IR_SendString("NEC learner ready\r\n");
  IR_SendString("Press a key once...\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* 超过 15 ms 没有新边沿，认为当前红外帧已经结束。 */
    if ((ir_capturing != 0U) &&
        ((HAL_GetTick() - ir_last_edge_ms) > IR_FRAME_GAP_MS))
    {
      __disable_irq();
      if ((ir_capturing != 0U) &&
          ((HAL_GetTick() - ir_last_edge_ms) > IR_FRAME_GAP_MS))
      {
        ir_capturing = 0U;
        ir_frame_ready = 1U;
      }
      __enable_irq();
    }

    if (ir_frame_ready != 0U)
    {
      IR_ProcessFrame();
      ir_frame_ready = 0U;
    }

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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI14|RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.HSI14State = RCC_HSI14_ON;
  RCC_OscInitStruct.HSI14CalibrationValue = 16;
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

static uint8_t IR_InRange(uint16_t value, uint16_t min, uint16_t max)
{
  return ((value >= min) && (value <= max)) ? 1U : 0U;
}

static void IR_SendString(const char *text)
{
  while (*text != '\0')
  {
    HAL_UART_Transmit(&huart1, (uint8_t *)text, 1U, 100U);
    text++;
  }
}

static void IR_SendHex8(uint8_t value)
{
  const char hex[] = "0123456789ABCDEF";
  uint8_t data[2];

  data[0] = (uint8_t)hex[(value >> 4) & 0x0FU];
  data[1] = (uint8_t)hex[value & 0x0FU];
  HAL_UART_Transmit(&huart1, data, 2U, 100U);
}

static void IR_SendHex16(uint16_t value)
{
  IR_SendHex8((uint8_t)(value >> 8));
  IR_SendHex8((uint8_t)value);
}

/*
 * EXTI 中断只负责测量边沿间隔，不在中断中发送串口或解析数据。
 * PA3 空闲为高电平，因此第一次下降沿作为一帧的开始。
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  uint16_t now;
  uint16_t duration;
  uint8_t current_level;

  if (GPIO_Pin != GPIO_PIN_3)
  {
    return;
  }

  now = __HAL_TIM_GET_COUNTER(&htim14);
  current_level = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3) == GPIO_PIN_SET) ? 1U : 0U;

  if (ir_capturing == 0U)
  {
    /* 下降沿表示新的低电平脉冲开始。 */
    if (current_level == 0U)
    {
      ir_capturing = 1U;
      ir_frame_ready = 0U;
      ir_capture_overflow = 0U;
      ir_edge_count = 0U;
      ir_last_timer = now;
      ir_last_edge_ms = HAL_GetTick();
    }
    return;
  }

  /* uint16_t 减法可以处理 TIM14 在 65535 到 0 的回绕。 */
  duration = (uint16_t)(now - ir_last_timer);
  ir_last_timer = now;
  ir_last_edge_ms = HAL_GetTick();

  if (ir_edge_count < IR_CAPTURE_MAX_EDGES)
  {
    /* duration 对应的是刚刚结束的电平。 */
    ir_duration_us[ir_edge_count] = duration;
    ir_level[ir_edge_count] = (current_level == 0U) ? 1U : 0U;
    ir_edge_count++;
  }
  else
  {
    ir_capture_overflow = 1U;
  }
}

static void IR_ProcessFrame(void)
{
  uint16_t duration[IR_CAPTURE_MAX_EDGES];
  uint8_t level[IR_CAPTURE_MAX_EDGES];
  uint16_t count;
  uint16_t i;
  uint8_t bytes[4] = {0U, 0U, 0U, 0U};
  uint8_t bit;
  uint16_t user_code;
  uint8_t key_code;
  uint8_t key_inverse;

  __disable_irq();
  count = ir_edge_count;
  if (count > IR_CAPTURE_MAX_EDGES)
  {
    count = IR_CAPTURE_MAX_EDGES;
  }
  for (i = 0U; i < count; i++)
  {
    duration[i] = ir_duration_us[i];
    level[i] = ir_level[i];
  }
  ir_edge_count = 0U;
  __enable_irq();
	
//检查数组是否溢出
  if (ir_capture_overflow != 0U)
  {
    IR_SendString("ERROR: raw buffer overflow\r\n");
    ir_capture_overflow = 0U;
    return;
  }

  /* NEC 重复码通常出现在长按时，不包含新的键位码。 */
  if ((count >= 3U) &&
      IR_InRange(duration[0], 8000U, 10000U) &&
      IR_InRange(duration[1], 1800U, 2800U) &&
      IR_InRange(duration[2], 350U, 800U))
  {
    IR_SendString("NEC repeat\r\n");
    return;
  }

  /* 检查完整帧长度：起始码 2 段 + 32 位数据的 64 段。 */
  if (count < 66U)
  {
    IR_SendString("ERROR: frame too short\r\n");
    return;
  }
	
//检查起始码
  if ((level[0] != 0U) || (level[1] != 1U) ||
      !IR_InRange(duration[0], NEC_LEADER_LOW_MIN_US, NEC_LEADER_LOW_MAX_US) ||
      !IR_InRange(duration[1], NEC_LEADER_HIGH_MIN_US, NEC_LEADER_HIGH_MAX_US))
  {
    IR_SendString("ERROR: not NEC leader\r\n");
    return;
  }
//解析 32 个数据位
  for (i = 0U; i < 32U; i++)
  {
    uint16_t low_time = duration[2U + i * 2U];
    uint16_t high_time = duration[3U + i * 2U];

    if ((level[2U + i * 2U] != 0U) ||
        (level[3U + i * 2U] != 1U) ||
        !IR_InRange(low_time, NEC_BIT_LOW_MIN_US, NEC_BIT_LOW_MAX_US))
    {
      IR_SendString("ERROR: invalid NEC bit\r\n");
      return;
    }

    if (IR_InRange(high_time, NEC_ZERO_HIGH_MIN_US, NEC_ZERO_HIGH_MAX_US))
    {
      bit = 0U;
    }
    else if (IR_InRange(high_time, NEC_ONE_HIGH_MIN_US, NEC_ONE_HIGH_MAX_US))
    {
      bit = 1U;
    }
    else
    {
      IR_SendString("ERROR: invalid bit width\r\n");
      return;
    }
		
	//把位组合成字节
  /* NEC 按低位优先发送。 */
	//bytes[0]：用户码字节 1
	//bytes[1]：用户码字节 2
	//bytes[2]：键位码
	//bytes[3]：键位反码
    if (bit != 0U)
    {
      bytes[i / 8U] |= (uint8_t)(1U << (i % 8U));
    }
  }

  user_code = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
  key_code = bytes[2];
  key_inverse = bytes[3];

  IR_SendString("NEC: User=0x");
  IR_SendHex16(user_code);
  IR_SendString(" Key=0x");
  IR_SendHex8(key_code);
  IR_SendString(" Inv=0x");
  IR_SendHex8(key_inverse);
  IR_SendString(" Raw=");
  IR_SendHex8(bytes[0]);
  IR_SendString(" ");
  IR_SendHex8(bytes[1]);
  IR_SendString(" ");
  IR_SendHex8(bytes[2]);
  IR_SendString(" ");
  IR_SendHex8(bytes[3]);

  if ((uint8_t)(key_code ^ key_inverse) == 0xFFU)
  {
    IR_SendString(" Check=OK\r\n");
  }
  else
  {
    IR_SendString(" Check=FAIL\r\n");
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

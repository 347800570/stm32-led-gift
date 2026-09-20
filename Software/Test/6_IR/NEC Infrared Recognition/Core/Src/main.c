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

typedef struct
{
  uint8_t code;
  const char *name;
} IR_KeyMapEntry;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define IR_CAPTURE_MAX_EDGES       70U
#define IR_FRAME_GAP_MS            15U

#define NEC_USER_BYTE_0            0x00U
#define NEC_USER_BYTE_1            0xFFU

#define NEC_LEADER_LOW_MIN_US      8000U
#define NEC_LEADER_LOW_MAX_US      10000U
#define NEC_LEADER_HIGH_MIN_US     3500U
#define NEC_LEADER_HIGH_MAX_US     5500U
#define NEC_BIT_LOW_MIN_US         350U
#define NEC_BIT_LOW_MAX_US         800U
#define NEC_ZERO_HIGH_MIN_US       350U
#define NEC_ZERO_HIGH_MAX_US       900U
#define NEC_ONE_HIGH_MIN_US        1200U
#define NEC_ONE_HIGH_MAX_US        2100U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* The EXTI callback writes pulse widths; the main loop decodes them. */
static volatile uint16_t ir_duration_us[IR_CAPTURE_MAX_EDGES];
static volatile uint8_t ir_level[IR_CAPTURE_MAX_EDGES];
static volatile uint16_t ir_edge_count;
static volatile uint8_t ir_capturing;
static volatile uint8_t ir_frame_ready;
static volatile uint8_t ir_capture_overflow;
static volatile uint16_t ir_last_timer;
static volatile uint32_t ir_last_edge_ms;

static const IR_KeyMapEntry ir_key_map[] =
{
  {0x45U, "POWER"},
  {0x47U, "MENU"},
  {0x44U, "TEST"},
  {0x40U, "+"},
  {0x43U, "RETURN"},
  {0x07U, "LEFT"},
  {0x15U, "PLAY"},
  {0x09U, "RIGHT"},
  {0x16U, "0"},
  {0x19U, "-"},
  {0x0DU, "C"},
  {0x0CU, "1"},
  {0x18U, "2"},
  {0x5EU, "3"},
  {0x08U, "4"},
  {0x1CU, "5"},
  {0x5AU, "6"},
  {0x42U, "7"},
  {0x52U, "8"},
  {0x4AU, "9"}
};

static const char *ir_last_key_name;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

static void IR_ProcessFrame(void);
static uint8_t IR_InRange(uint16_t value, uint16_t min, uint16_t max);
static const char *IR_FindKeyName(uint8_t key_code);
static void IR_SendString(const char *text);
static void IR_SendHex8(uint8_t value);

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

  /* TIM14 runs at 1 MHz, so one timer count is approximately 1 us. */
  if (HAL_TIM_Base_Start(&htim14) != HAL_OK)
  {
    Error_Handler();
  }

  IR_SendString("NEC recognition ready\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* No edge for 15 ms marks the end of the current NEC frame. */
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

static const char *IR_FindKeyName(uint8_t key_code)
{
  uint8_t i;

  for (i = 0U; i < (sizeof(ir_key_map) / sizeof(ir_key_map[0])); i++)
  {
    if (ir_key_map[i].code == key_code)
    {
      return ir_key_map[i].name;
    }
  }

  return NULL;
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
  static const char hex[] = "0123456789ABCDEF";
  uint8_t data[2];

  data[0] = (uint8_t)hex[(value >> 4) & 0x0FU];
  data[1] = (uint8_t)hex[value & 0x0FU];
  HAL_UART_Transmit(&huart1, data, 2U, 100U);
}

/*
 * PA3 is idle high. The first falling edge starts a frame. Keep this callback
 * short: only measure and store pulse widths here.
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

  /* Unsigned subtraction handles the 16-bit timer wraparound. */
  duration = (uint16_t)(now - ir_last_timer);
  ir_last_timer = now;
  ir_last_edge_ms = HAL_GetTick();

  if (ir_edge_count < IR_CAPTURE_MAX_EDGES)
  {
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
  uint8_t key_code;
  uint8_t key_inverse;
  const char *key_name;

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

  if (ir_capture_overflow != 0U)
  {
    ir_capture_overflow = 0U;
    IR_SendString("INVALID NEC FRAME: buffer overflow\r\n");
    return;
  }

  /* A held button sends an NEC repeat frame without a new command byte. */
  if ((count >= 3U) &&
      IR_InRange(duration[0], 8000U, 10000U) &&
      IR_InRange(duration[1], 1800U, 2800U) &&
      IR_InRange(duration[2], 350U, 800U))
  {
    if (ir_last_key_name != NULL)
    {
      IR_SendString("REPEAT: ");
      IR_SendString(ir_last_key_name);
      IR_SendString("\r\n");
    }
    else
    {
      IR_SendString("REPEAT: no previous key\r\n");
    }
    return;
  }

  if (count < 66U)
  {
    IR_SendString("INVALID NEC FRAME: too short\r\n");
    return;
  }

  if ((level[0] != 0U) || (level[1] != 1U) ||
      !IR_InRange(duration[0], NEC_LEADER_LOW_MIN_US, NEC_LEADER_LOW_MAX_US) ||
      !IR_InRange(duration[1], NEC_LEADER_HIGH_MIN_US, NEC_LEADER_HIGH_MAX_US))
  {
    IR_SendString("INVALID NEC FRAME: bad leader\r\n");
    return;
  }

  for (i = 0U; i < 32U; i++)
  {
    uint16_t low_time = duration[2U + i * 2U];
    uint16_t high_time = duration[3U + i * 2U];

    if ((level[2U + i * 2U] != 0U) ||
        (level[3U + i * 2U] != 1U) ||
        !IR_InRange(low_time, NEC_BIT_LOW_MIN_US, NEC_BIT_LOW_MAX_US))
    {
      IR_SendString("INVALID NEC FRAME: bad bit timing\r\n");
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
      IR_SendString("INVALID NEC FRAME: bad bit width\r\n");
      return;
    }

    /* NEC sends the least significant bit of each byte first. */
    if (bit != 0U)
    {
      bytes[i / 8U] |= (uint8_t)(1U << (i % 8U));
    }
  }

  key_code = bytes[2];
  key_inverse = bytes[3];

  if ((bytes[0] != NEC_USER_BYTE_0) || (bytes[1] != NEC_USER_BYTE_1))
  {
    IR_SendString("INVALID USER CODE: 0x");
    IR_SendHex8(bytes[0]);
    IR_SendHex8(bytes[1]);
    IR_SendString("\r\n");
    return;
  }

  if ((uint8_t)(key_code ^ key_inverse) != 0xFFU)
  {
    IR_SendString("INVALID NEC FRAME: command check failed\r\n");
    return;
  }

  key_name = IR_FindKeyName(key_code);
  if (key_name == NULL)
  {
    IR_SendString("UNKNOWN: 0x");
    IR_SendHex8(key_code);
    IR_SendString("\r\n");
    return;
  }

  ir_last_key_name = key_name;
  IR_SendString("KEY: ");
  IR_SendString(key_name);
  IR_SendString("\r\n");
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

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
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  DS18B20_STATUS_OK = 0,
  DS18B20_STATUS_NO_DEVICE,
  DS18B20_STATUS_CRC_ERROR
} DS18B20_StatusTypeDef;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DS18B20_CMD_SKIP_ROM        0xCCU			//不指定传感器地址，让总线上唯一的DS18B20接收后续命令
#define DS18B20_CMD_CONVERT_T       0x44U     //启动一次温度测量和模数转换。
#define DS18B20_CMD_READ_SCRATCHPAD 0xBEU			//读取DS18B20内部的9字节暂存器。
#define DS18B20_SCRATCHPAD_SIZE     9U
#define DS18B20_CONVERSION_TIME_MS  750U
#define DS18B20_UART_TIMEOUT_MS     100U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Keep the latest raw value visible in the debugger Watch window. */
volatile int16_t ds18b20_raw_temperature;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void DelayUs(uint16_t microseconds);
static uint8_t DS18B20_Reset(void);
static void DS18B20_WriteBit(uint8_t bit_value);
static uint8_t DS18B20_ReadBit(void);
static void DS18B20_WriteByte(uint8_t data);
static uint8_t DS18B20_ReadByte(void);
static uint8_t DS18B20_CalculateCrc(const uint8_t *data, uint8_t length);
static DS18B20_StatusTypeDef DS18B20_StartConversion(void);
static DS18B20_StatusTypeDef DS18B20_ReadTemperature(int16_t *raw_temperature);
static void UART_SendString(const char *text);
static void UART_SendTemperature(int16_t raw_temperature);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief Produce a blocking microsecond delay with the 1 MHz TIM14 counter.
		@brief TIM14每计数一次就过1us，所以这里实现了微秒级延时
  */ 
static void DelayUs(uint16_t microseconds)
{
  uint16_t start = (uint16_t)__HAL_TIM_GET_COUNTER(&htim14);

  while ((uint16_t)((uint16_t)__HAL_TIM_GET_COUNTER(&htim14) - start) < microseconds)
  {
  }
}

/**
  * @brief Reset the 1-Wire bus and detect the DS18B20 presence pulse.
		释放DQ 5 μs
		拉低DQ 480 μs
		释放DQ
		等待70 μs
		读取DQ
		再等待410 μs
		DQ为低电平 → DS18B20返回了存在脉冲
		DQ为高电平 → 没有传感器应答
  * @retval 1 when a sensor responds, otherwise 0.
  */
static uint8_t DS18B20_Reset(void)
{
  uint8_t presence;
  uint32_t primask = __get_PRIMASK();																					 //PRIMASK是 Cortex-M0内核中的一个寄存器，保存当前的中断开关状态

  __disable_irq();
  HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_SET);       //SET是置一，RESET是置零
  DelayUs(5U);
  HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_RESET);
  DelayUs(480U);
  HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_SET);
  DelayUs(70U);

  presence = (HAL_GPIO_ReadPin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
  DelayUs(410U);

  if (primask == 0U)																														//如果中断原来是开启的，就重新开启。如果中断原来就是关闭的，则保持关闭。
  {
    __enable_irq();
  }

  return presence;
}

/**
  * @brief Write one bit into a 1-Wire time slot.
		写1：拉低6 μs，然后释放64 μs
		写0：拉低60 μs，然后释放10 μs
  */
static void DS18B20_WriteBit(uint8_t bit_value)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_RESET);

  if (bit_value != 0U)
  {
    DelayUs(6U);
    HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_SET);
    DelayUs(64U);
  }
  else
  {
    DelayUs(60U);
    HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_SET);
    DelayUs(10U);
  }

  if (primask == 0U)
  {
    __enable_irq();
  }
}

/**
  * @brief Read one bit from a 1-Wire time slot.
		MCU短暂拉低PA2，发起读取
		→ MCU释放PA2
		→ DS18B20决定保持低电平还是释放总线
		→ MCU读取PA2电平
		→ 返回0或1
  */
static uint8_t DS18B20_ReadBit(void)
{
  uint8_t bit_value;
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_RESET);
  DelayUs(3U);
  HAL_GPIO_WritePin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin, GPIO_PIN_SET);
  DelayUs(10U);
  bit_value = (HAL_GPIO_ReadPin(DS18B20_DQ_GPIO_Port, DS18B20_DQ_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  DelayUs(53U);

  if (primask == 0U)
  {
    __enable_irq();
  }

  return bit_value;
}

/**
  * @brief Write one byte, least-significant bit first as required by 1-Wire.
  */
static void DS18B20_WriteByte(uint8_t data)
{
  for (uint8_t bit = 0U; bit < 8U; bit++)
  {
    DS18B20_WriteBit(data & 0x01U);
    data >>= 1U;
  }
}

/**
  * @brief Read one byte, least-significant bit first.
  */
static uint8_t DS18B20_ReadByte(void)
{
  uint8_t data = 0U;

  for (uint8_t bit = 0U; bit < 8U; bit++)
  {
    data |= (uint8_t)(DS18B20_ReadBit() << bit);
  }

  return data;
}

/**
  * @brief Calculate the Dallas/Maxim 1-Wire CRC-8 value.
  */
static uint8_t DS18B20_CalculateCrc(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0U;

  for (uint8_t index = 0U; index < length; index++)
  {
    uint8_t current_byte = data[index];

    for (uint8_t bit = 0U; bit < 8U; bit++)
    {
      uint8_t mix = (uint8_t)((crc ^ current_byte) & 0x01U);
      crc >>= 1U;
      if (mix != 0U)
      {
        crc ^= 0x8CU;
      }
      current_byte >>= 1U;
    }
  }

  return crc;
}

/**
  * @brief Ask the only sensor on the bus to start a temperature conversion.
  */
static DS18B20_StatusTypeDef DS18B20_StartConversion(void)
{
  if (DS18B20_Reset() == 0U)
  {
    return DS18B20_STATUS_NO_DEVICE;
  }

  DS18B20_WriteByte(DS18B20_CMD_SKIP_ROM);
  DS18B20_WriteByte(DS18B20_CMD_CONVERT_T);
  return DS18B20_STATUS_OK;
}

/**
  * @brief Read and validate the sensor scratchpad, then return the raw value.
  */
static DS18B20_StatusTypeDef DS18B20_ReadTemperature(int16_t *raw_temperature)
{
  uint8_t scratchpad[DS18B20_SCRATCHPAD_SIZE];

  if (DS18B20_Reset() == 0U)
  {
    return DS18B20_STATUS_NO_DEVICE;
  }

  DS18B20_WriteByte(DS18B20_CMD_SKIP_ROM);
  DS18B20_WriteByte(DS18B20_CMD_READ_SCRATCHPAD);

  for (uint8_t index = 0U; index < DS18B20_SCRATCHPAD_SIZE; index++)
  {
    scratchpad[index] = DS18B20_ReadByte();
  }

  if (DS18B20_CalculateCrc(scratchpad, 8U) != scratchpad[8])
  {
    return DS18B20_STATUS_CRC_ERROR;
  }

  *raw_temperature = (int16_t)(((uint16_t)scratchpad[1] << 8U) | scratchpad[0]);    //把两个8位字节组合成一个16位温度原始值
  return DS18B20_STATUS_OK;
}

/**
  * @brief Send a zero-terminated message over USART1.
  */
static void UART_SendString(const char *text)
{
  (void)HAL_UART_Transmit(&huart1,
                          (const uint8_t *)text,
                          (uint16_t)strlen(text),
                          DS18B20_UART_TIMEOUT_MS);
}

/**
  * @brief Print a signed raw temperature with its exact 0.0625 C resolution.
  */
static void UART_SendTemperature(int16_t raw_temperature)
{
  char message[40];
  int32_t signed_raw = raw_temperature;
  uint32_t magnitude = (signed_raw < 0) ? (uint32_t)(-signed_raw) : (uint32_t)signed_raw;			//取温度的绝对值
  uint32_t integer_part = magnitude / 16U;																										//计算整数部分
  uint32_t fractional_part = (magnitude % 16U) * 625U;																				//计算小数部分
  int length;

  length = snprintf(message,
                    sizeof(message),
                    "Temperature: %s%lu.%04lu C\r\n",																					
										//%s：负数时插入 "-"，正数时插入空字符串。
										//%lu：输出整数部分。
										//%04lu：小数部分固定输出4位，不足时前面补零。
										//\r\n：串口换行。
                    (signed_raw < 0) ? "-" : "",
                    (unsigned long)integer_part,
                    (unsigned long)fractional_part);

  if (length > 0)
  {
    (void)HAL_UART_Transmit(&huart1,
                            (const uint8_t *)message,
                            (uint16_t)length,
                            DS18B20_UART_TIMEOUT_MS);
  }
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
  MX_TIM14_Init();
  /* USER CODE BEGIN 2 */

  /* TIM14 counts at 1 MHz, so one counter tick equals one microsecond. */
  if (HAL_TIM_Base_Start(&htim14) != HAL_OK)
  {
    Error_Handler();
  }

  UART_SendString("DS18B20 temperature test started\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    DS18B20_StatusTypeDef status;
    int16_t raw_temperature;

    status = DS18B20_StartConversion();
    if (status == DS18B20_STATUS_NO_DEVICE)
    {
      UART_SendString("DS18B20 error: no presence\r\n");
      HAL_Delay(1000U);
      continue;
    }

    /* The power-on default is 12-bit resolution, requiring up to 750 ms. */
    HAL_Delay(DS18B20_CONVERSION_TIME_MS);
    status = DS18B20_ReadTemperature(&raw_temperature);

    if (status == DS18B20_STATUS_OK)
    {
      ds18b20_raw_temperature = raw_temperature;
      UART_SendTemperature(raw_temperature);
    }
    else if (status == DS18B20_STATUS_CRC_ERROR)
    {
      UART_SendString("DS18B20 error: CRC mismatch\r\n");
    }
    else
    {
      UART_SendString("DS18B20 error: no presence\r\n");
    }

    /* Together with conversion time, this gives roughly one report per second. */
    HAL_Delay(250U);
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

#include "temperature.h"
#include "tim.h"

/* 单个、外部供电的 DS18B20；DQ 为 PA2 开漏，板上必须有外部上拉。 */
typedef enum { START_CONVERSION, WAIT_CONVERSION, READ_SCRATCHPAD } TempStage;
static TempStage stage;
static Temperature_Status status;
static uint32_t cycle_at, conversion_at, last_good_at, sample_count;
static int32_t filtered_q8;
static int16_t latest_raw;
static uint8_t has_sample, first_cycle, scratch[9], read_index;

static void DelayUs(uint16_t us)
{
    uint16_t start = (uint16_t)__HAL_TIM_GET_COUNTER(&htim14);
    /* 共用 TIM14，只读取时间差，绝不清零，以免破坏红外边沿计时。 */
    while ((uint16_t)((uint16_t)__HAL_TIM_GET_COUNTER(&htim14)-start) < us) { }
}

static uint8_t BusHigh(void)
{
    return (DS18B20_DQ_GPIO_Port->IDR & DS18B20_DQ_Pin) != 0U;
}

static void Release(void) { DS18B20_DQ_GPIO_Port->BSRR = DS18B20_DQ_Pin; }
static void PullLow(void) { DS18B20_DQ_GPIO_Port->BRR = DS18B20_DQ_Pin; }

static uint8_t ResetBus(void)
{
    uint8_t presence;
    uint32_t saved;
    Release();
    DelayUs(5U);
    if (!BusHigh()) return 0U; /* 拒绝总线短路/持续低电平。 */
    PullLow();
    DelayUs(480U); /* 这段保持中断开放，避免丢失红外边沿。 */
    saved = __get_PRIMASK();
    __disable_irq();
    Release();
    DelayUs(70U);
    presence = !BusHigh();
    __set_PRIMASK(saved);
    DelayUs(410U);
    return presence && BusHigh();
}

static void WriteBit(uint8_t bit)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    PullLow();
    DelayUs(bit ? 6U : 60U);
    Release();
    __set_PRIMASK(saved);
    DelayUs(bit ? 64U : 10U);
}

static uint8_t ReadBit(void)
{
    uint8_t bit;
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    PullLow();
    DelayUs(3U);
    Release();
    DelayUs(10U);
    bit = BusHigh();
    __set_PRIMASK(saved);
    DelayUs(57U);
    return bit;
}

static void WriteByte(uint8_t byte)
{
    uint8_t i;
    for (i = 0U; i < 8U; i++) { WriteBit(byte & 1U); byte >>= 1U; }
}

static uint8_t ReadByte(void)
{
    uint8_t byte = 0U, i;
    for (i = 0U; i < 8U; i++) byte |= (uint8_t)(ReadBit() << i);
    return byte;
}

static uint8_t Crc8(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0U, i, j, byte, mix;
    for (i = 0U; i < length; i++)
    {
        byte = data[i];
        for (j = 0U; j < 8U; j++)
        {
            mix = (crc ^ byte) & 1U;
            crc >>= 1U;
            if (mix) crc ^= 0x8CU;
            byte >>= 1U;
        }
    }
    return crc;
}

static void FinishRead(void)
{
    int16_t raw;
    uint16_t word;
    uint8_t resolution;
    stage = START_CONVERSION;
    if (Crc8(scratch, 8U) != scratch[8]) { status = TEMP_CRC_ERROR; return; }
    /* 配置寄存器固定保留位校验，可拦截全零假数据；不修改传感器寄存器。 */
    if ((scratch[4] & 0x9FU) != 0x1FU) { status = TEMP_BAD_DATA; return; }
    word = (uint16_t)((uint16_t)scratch[1] << 8U) | scratch[0];
    resolution = (scratch[4] >> 5U) & 3U;
    word &= (uint16_t)(0xFFFFU << (3U-resolution));
    raw = (int16_t)word;
    if (raw < -880 || raw > 2000) { status = TEMP_BAD_DATA; return; }
    latest_raw = raw;
    /* 第一次有效值直接采用，以后每秒向新温度靠近四分之一。 */
    if (!has_sample || Temperature_IsFault()) filtered_q8 = (int32_t)raw * 16;
    else filtered_q8 += ((int32_t)raw*16-filtered_q8)/4;
    has_sample = 1U;
    last_good_at = HAL_GetTick();
    sample_count++;
    status = TEMP_OK;
}

void Temperature_Init(void)
{
    stage = START_CONVERSION;
    status = TEMP_WAITING;
    first_cycle = 1U;
    has_sample = 0U;
    sample_count = 0U;
    filtered_q8 = 0;
    latest_raw = 0;
    cycle_at = last_good_at = HAL_GetTick();
    Release();
}

void Temperature_Task(void)
{
    uint32_t now = HAL_GetTick();
    switch (stage)
    {
    case START_CONVERSION:
        if (!first_cycle && now-cycle_at < 1000U) return;
        first_cycle = 0U;
        cycle_at = now;
        if (!ResetBus()) { status = TEMP_NO_DEVICE; return; }
        WriteByte(0xCCU); /* Skip ROM：总线上只有一颗传感器。 */
        WriteByte(0x44U); /* Convert T：启动转换后立即返回。 */
        conversion_at = HAL_GetTick();
        stage = WAIT_CONVERSION;
        break;
    case WAIT_CONVERSION:
        if (now-conversion_at < 760U) return;
        if (!ReadBit()) { status = TEMP_NOT_READY; stage = START_CONVERSION; return; }
        if (!ResetBus()) { status = TEMP_NO_DEVICE; stage = START_CONVERSION; return; }
        WriteByte(0xCCU);
        WriteByte(0xBEU);
        read_index = 0U;
        stage = READ_SCRATCHPAD;
        break;
    case READ_SCRATCHPAD:
        /* 每次任务只读一个字节，期间主循环仍可处理红外、动画和串口。 */
        scratch[read_index++] = ReadByte();
        if (read_index == 9U) FinishRead();
        break;
    default: stage = START_CONVERSION; break;
    }
}

uint8_t Temperature_HasSample(void) { return has_sample; }
uint8_t Temperature_IsFault(void) { return HAL_GetTick()-last_good_at >= 5000U; }
int16_t Temperature_FilteredRaw(void) { return (int16_t)(filtered_q8/16); }
int16_t Temperature_LatestRaw(void) { return latest_raw; }
Temperature_Status Temperature_GetStatus(void) { return status; }
uint32_t Temperature_SampleCount(void) { return sample_count; }

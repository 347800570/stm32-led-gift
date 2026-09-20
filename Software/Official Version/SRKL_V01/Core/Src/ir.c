#include "ir.h"
#include "tim.h"

/* PA3 输入的是接收头解调后的包络；这里识别 00 FF 用户码的 NEC。 */
typedef enum { IDLE, HEADER_LOW, HEADER_HIGH, BIT_LOW, BIT_HIGH,
               STOP_LOW, REPEAT_LOW } State;
static State state;
static uint16_t last_timer;
static uint32_t last_edge_ms, bits, last_valid_ms;
static uint8_t bit_index, last_code, repeat_valid, enabled;
static volatile IR_Event queue[8];
static volatile uint8_t head, tail;
static volatile uint32_t errors;

static uint8_t InRange(uint16_t n, uint16_t lo, uint16_t hi)
{ return n >= lo && n <= hi; }

static void Publish(uint8_t code, uint8_t repeat, uint32_t now)
{
    uint8_t next = (uint8_t)((head+1U)%8U);
    if (next == tail) { errors++; return; }
    queue[head].code = code;
    queue[head].repeat = repeat;
    queue[head].time_ms = now;
    __DMB();
    head = next;
}

void IR_Init(void)
{
    state = IDLE;
    head = tail = repeat_valid = 0U;
    errors = 0U;
    if (HAL_TIM_Base_Start(&htim14) != HAL_OK) Error_Handler();
    enabled = 1U;
}

uint8_t IR_Poll(IR_Event *event)
{
    uint8_t slot = tail;
    if (slot == head) return 0U;
    event->code = queue[slot].code;
    event->repeat = queue[slot].repeat;
    event->time_ms = queue[slot].time_ms;
    __DMB();
    tail = (uint8_t)((slot+1U)%8U);
    return 1U;
}

uint32_t IR_ErrorCount(void) { return errors; }

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    uint32_t now;
    uint16_t timer, width;
    uint8_t high, valid = 1U;
    if (pin != GPIO_PIN_3 || !enabled) return;
    now = HAL_GetTick();
    timer = (uint16_t)__HAL_TIM_GET_COUNTER(&htim14);
    high = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3) == GPIO_PIN_SET;
    width = (uint16_t)(timer-last_timer);
    /* 毫秒间隔辅助判断空闲，防止 16 位计数器多次回绕误判。 */
    if (now-last_edge_ms > 15U)
    {
        if (state != IDLE) { errors++; repeat_valid = 0U; }
        state = IDLE;
    }
    last_edge_ms = now;
    last_timer = timer;
    switch (state)
    {
    case IDLE:
        if (!high) state = HEADER_LOW;
        break;
    case HEADER_LOW:
        if (high && InRange(width,8000U,10000U)) state = HEADER_HIGH;
        else valid = 0U;
        break;
    case HEADER_HIGH:
        if (!high && InRange(width,3500U,5500U))
        { state = BIT_LOW; bits = 0U; bit_index = 0U; }
        else if (!high && InRange(width,1800U,2800U)) state = REPEAT_LOW;
        else valid = 0U;
        break;
    case BIT_LOW:
        if (high && InRange(width,350U,800U)) state = BIT_HIGH;
        else valid = 0U;
        break;
    case BIT_HIGH:
        if (high) { valid = 0U; break; }
        if (InRange(width,1200U,2100U)) bits |= 1UL << bit_index;
        else if (!InRange(width,350U,900U)) { valid = 0U; break; }
        bit_index++;
        state = bit_index == 32U ? STOP_LOW : BIT_LOW;
        break;
    case STOP_LOW:
        if (high && InRange(width,350U,800U) &&
            (bits & 0xFFFFU) == 0xFF00U &&
            ((uint8_t)(bits >> 16U) ^ (uint8_t)(bits >> 24U)) == 0xFFU)
        {
            last_code = (uint8_t)(bits >> 16U);
            last_valid_ms = now;
            repeat_valid = 1U;
            Publish(last_code, 0U, now);
            state = IDLE;
        }
        else valid = 0U;
        break;
    case REPEAT_LOW:
        if (high && InRange(width,350U,800U))
        {
            /* 超时的重复帧不能复用很久以前的按键。 */
            if (repeat_valid && now-last_valid_ms <= 180U)
            {
                Publish(last_code, 1U, now);
                last_valid_ms = now;
            }
            else repeat_valid = 0U;
            state = IDLE;
        }
        else valid = 0U;
        break;
    default: valid = 0U; break;
    }
    if (!valid)
    {
        errors++;
        repeat_valid = 0U;
        state = high ? IDLE : HEADER_LOW;
    }
}

const char *IR_KeyName(uint8_t code)
{
    static const struct { uint8_t code; const char *name; } keys[] = {
        {0x45,"POWER"},{0x47,"MENU"},{0x44,"TEST"},{0x40,"+"},
        {0x43,"RETURN"},{0x07,"LEFT"},{0x15,"PLAY"},{0x09,"RIGHT"},
        {0x16,"0"},{0x19,"-"},{0x0D,"C"},{0x0C,"1"},{0x18,"2"},
        {0x5E,"3"},{0x08,"4"},{0x1C,"5"},{0x5A,"6"},
        {0x42,"7"},{0x52,"8"},{0x4A,"9"}
    };
    uint32_t i;
    for (i = 0U; i < sizeof(keys)/sizeof(keys[0]); i++)
        if (keys[i].code == code) return keys[i].name;
    return "UNKNOWN";
}

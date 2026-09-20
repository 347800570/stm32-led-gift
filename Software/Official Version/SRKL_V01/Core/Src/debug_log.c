#include "debug_log.h"
#include "usart.h"

/* 只在主循环使用。逐字节检查 TXE，不等待串口发送，避免动画停顿。 */
static uint8_t buffer[256];
static uint8_t head, tail;
static uint8_t dropped;

static void Put(uint8_t c)
{
    uint8_t next = (uint8_t)(head+1U);
    if (next == tail) { dropped = 1U; return; }
    buffer[head] = c;
    head = next;
}
void Log_Text(const char *text)
{
    while (*text) Put((uint8_t)*text++);
}
void Log_Number(uint32_t value)
{
    char digits[10];
    uint8_t n = 0U;
    do { digits[n++] = (char)('0'+value%10U); value /= 10U; } while (value);
    while (n) Put((uint8_t)digits[--n]);
}
void Log_Hex8(uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    Put((uint8_t)hex[value>>4U]); Put((uint8_t)hex[value&15U]);
}
void Log_Task(void)
{
    if (tail != head && __HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE))
    {
        huart1.Instance->TDR = buffer[tail];
        tail++;
    }
    if (tail == head && dropped)
    {
        dropped = 0U;
        Log_Text("\r\nLOG OVERFLOW\r\n");
    }
}

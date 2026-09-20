#ifndef MPLT_DEBUG_LOG_H
#define MPLT_DEBUG_LOG_H
#include <stdint.h>
void Log_Text(const char *text);
void Log_Number(uint32_t value);
void Log_Hex8(uint8_t value);
void Log_Task(void);
#endif

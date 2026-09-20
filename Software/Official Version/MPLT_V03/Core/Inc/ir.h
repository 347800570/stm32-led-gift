#ifndef MPLT_IR_H
#define MPLT_IR_H
#include <stdint.h>
typedef struct { uint8_t code, repeat; uint32_t time_ms; } IR_Event;
void IR_Init(void);
uint8_t IR_Poll(IR_Event *event);
const char *IR_KeyName(uint8_t code);
uint32_t IR_ErrorCount(void);
#endif

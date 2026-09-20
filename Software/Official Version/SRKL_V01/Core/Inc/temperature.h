#ifndef MPLT_TEMPERATURE_H
#define MPLT_TEMPERATURE_H
#include <stdint.h>
typedef enum {
    TEMP_WAITING, TEMP_OK, TEMP_NO_DEVICE, TEMP_CRC_ERROR,
    TEMP_BAD_DATA, TEMP_NOT_READY
} Temperature_Status;
void Temperature_Init(void);
void Temperature_Task(void);
uint8_t Temperature_HasSample(void);
uint8_t Temperature_IsFault(void);
/* 单位为 1/16 摄氏度；显示用平滑值，诊断用最新实测值。 */
int16_t Temperature_FilteredRaw(void);
int16_t Temperature_LatestRaw(void);
Temperature_Status Temperature_GetStatus(void);
uint32_t Temperature_SampleCount(void);
#endif

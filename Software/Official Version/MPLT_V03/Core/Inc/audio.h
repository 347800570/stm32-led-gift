#ifndef MPLT_AUDIO_H
#define MPLT_AUDIO_H
#include <stdint.h>

/* 单位为12位ADC码值的平均交流幅度。按实测安静环境调整，不是电压。 */
#define AUDIO_NOISE_GATE 12U
#define AUDIO_DMA_SAMPLES 128U
typedef enum { AUDIO_WAITING, AUDIO_OK, AUDIO_FAULT } Audio_Status;
void Audio_Init(void);
void Audio_Task(void);
void Audio_SetSensitivity(uint8_t index); /* 0..4：从低到高 */
uint8_t Audio_GetLevel(void);             /* 0..255，故障时为0 */
uint8_t Audio_GetPulseLevel(void);        /* 9号鼓点触发的独立灯光脉冲 */
Audio_Status Audio_GetStatus(void);
uint16_t Audio_GetAmplitude(void);
uint16_t Audio_GetBias(void);
uint16_t Audio_GetClippedSamples(void);   /* 最近一块的接近电源轨采样数 */
uint32_t Audio_GetErrorCount(void);
uint32_t Audio_GetBlockCount(void);
#endif

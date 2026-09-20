#include "app.h"
#include "effects.h"
#include "ir.h"
#include "debug_log.h"
#include "main.h"
#include "temperature.h"
#include "audio.h"
#include "beat.h"

static LED_Color pixels[LED_COUNT];
static uint8_t power_on, mode, brightness, speed, dirty;
static uint8_t sensitivity;
static uint32_t frame_at, pressed_at, adjusted_at, diagnostics_at;
static uint32_t led_errors_seen, ir_errors_seen;
static uint8_t beat_log_pending;
static const uint8_t mode_keys[9] = {0x0C,0x18,0x5E,0x08,0x1C,0x5A,0x42,0x52,0x4A};

static void Status(void)
{
    Log_Text(power_on ? "STATE ON MODE=" : "STATE OFF MODE=");
    Log_Number(mode);
    Log_Text(" BRIGHTNESS="); Log_Number(brightness);
    Log_Text("% SPEED="); Log_Number(speed+1U);
    Log_Text(" SENS="); Log_Number(sensitivity+1U);
    Log_Text("\r\n");
}

static void HandleKey(const IR_Event *e)
{
    uint8_t i;
    uint8_t adjust = e->code == 0x40U || e->code == 0x19U ||
                     e->code == 0x07U || e->code == 0x09U;
    if (e->repeat)
    {
        /* 长按仅允许调整键；首次等待 350 ms，之后至少间隔 160 ms。 */
        if (!adjust || !power_on || e->time_ms-pressed_at < 350U ||
            e->time_ms-adjusted_at < 160U) return;
    }
    else pressed_at = e->time_ms;

    Log_Text(e->repeat ? "REPEAT " : "KEY ");
    Log_Text(IR_KeyName(e->code));
    Log_Text(" 0x"); Log_Hex8(e->code); Log_Text("\r\n");
    if (e->code == 0x45U)
    {
        power_on = !power_on;
        frame_at = HAL_GetTick();
        dirty = 1U;
        Status();
        return;
    }
    /* 关灯时不会修改模式或参数；只有 POWER 可以唤醒。 */
    if (!power_on) { Log_Text("IGNORED: OFF\r\n"); return; }
    for (i = 0U; i < sizeof(mode_keys)/sizeof(mode_keys[0]); i++)
    {
        if (e->code == mode_keys[i])
        {
            mode = i+1U;
            Effects_Reset();
            frame_at = HAL_GetTick();
            dirty = 1U;
            Status();
            return;
        }
    }
    if (adjust)
    {
        adjusted_at = e->time_ms;
        if (e->code == 0x40U && brightness < LED_MAX_BRIGHTNESS) brightness += 3U;
        if (e->code == 0x19U && brightness > 3U) brightness -= 3U;
        if (mode == 8U || mode == 9U)
        {
            if (e->code == 0x07U && sensitivity > 0U) sensitivity--;
            if (e->code == 0x09U && sensitivity < 4U) sensitivity++;
            Audio_SetSensitivity(sensitivity);
        }
        else
        {
            if (e->code == 0x07U && speed > 0U) speed--;
            if (e->code == 0x09U && speed < 4U) speed++;
        }
        dirty = 1U;
        Status();
    }
    else Log_Text("UNASSIGNED IN SRKL V01\r\n");
}

void App_Init(void)
{
    power_on = 1U; mode = 3U; brightness = 15U; speed = 2U; dirty = 1U;
    sensitivity = 2U;
    frame_at = HAL_GetTick();
    LED_Init();
    Effects_Reset();
    IR_Init();
    /* TIM14 已由 IR_Init 启动，温度模块不能重复清零它。 */
    Temperature_Init();
    Audio_Init();
    Audio_SetSensitivity(sensitivity);
    Log_Text("SRKL V01 READY; 74 LEDs; NEC user=00 FF\r\n");
    Status();
}

static void TemperatureLog(void)
{
    static const char *const names[] = {
        "WAITING", "OK", "NO_DEVICE", "CRC_ERROR", "BAD_DATA", "NOT_READY"
    };
    int32_t raw = Temperature_LatestRaw();
    uint32_t magnitude, fraction;
    Log_Text("TEMP "); Log_Text(names[Temperature_GetStatus()]);
    if (Temperature_HasSample())
    {
        Log_Text(" LAST=");
        if (raw < 0) Log_Text("-");
        magnitude = (uint32_t)(raw < 0 ? -raw : raw);
        Log_Number(magnitude/16U); Log_Text(".");
        fraction = (magnitude%16U)*100U/16U;
        if (fraction < 10U) Log_Text("0");
        Log_Number(fraction); Log_Text("C");
    }
    Log_Text(" SAMPLES="); Log_Number(Temperature_SampleCount());
    if (Temperature_IsFault()) Log_Text(" FAULT");
    Log_Text("\r\n");
}

static void BeatLog(void)
{
    Beat_Diagnostics d;
    Beat_TakeDiagnostics(&d);
    Log_Text("BEAT WIN VALID="); Log_Number(d.valid);
    Log_Text(" PEAK="); Log_Number(d.peak);
    Log_Text(" TH="); Log_Number(d.threshold);
    Log_Text(" BG="); Log_Number(d.background);
    Log_Text(" SENS="); Log_Number(d.sensitivity);
    Log_Text(" HIT="); Log_Number(d.results[BEAT_HIT]);
    Log_Text(" BELOW="); Log_Number(d.results[BEAT_BELOW]);
    Log_Text(" RISE="); Log_Number(d.results[BEAT_NO_RISE]);
    Log_Text(" ARM="); Log_Number(d.results[BEAT_NOT_ARMED]);
    Log_Text(" LOCK="); Log_Number(d.results[BEAT_LOCKED]);
    Log_Text("\r\n");
}

static void AudioLog(void)
{
    static const char *const names[] = {"WAITING", "OK", "FAULT"};
    Log_Text("AUDIO "); Log_Text(names[Audio_GetStatus()]);
    Log_Text(" AMP="); Log_Number(Audio_GetAmplitude());
    Log_Text(" LEVEL="); Log_Number(Audio_GetLevel());
    Log_Text(" LEVEL9="); Log_Number(Audio_GetPulseLevel());
    Log_Text(" LOW="); Log_Number(Beat_Energy());
    Log_Text(" BG="); Log_Number(Beat_Background());
    Log_Text(" BEATS="); Log_Number(Beat_Count());
    Log_Text(" BIAS="); Log_Number(Audio_GetBias());
    Log_Text(" CLIP="); Log_Number(Audio_GetClippedSamples());
    Log_Text(" ERR="); Log_Number(Audio_GetErrorCount());
    Log_Text(" BLOCKS="); Log_Number(Audio_GetBlockCount());
    Log_Text("\r\n");
}

void App_Task(void)
{
    IR_Event event;
    uint32_t now, elapsed, errors;
    uint8_t ready;
    Log_Task();
    while (IR_Poll(&event)) HandleKey(&event);
    Audio_Task();
    /* 等待转换期间立即返回；关灯及其他模式下也持续更新温度。 */
    Temperature_Task();
    now = HAL_GetTick();
    ready = LED_Ready();
    errors = LED_ErrorCount();
    if (errors != led_errors_seen)
    {
        led_errors_seen = errors;
        dirty = 1U;
        Log_Text("LED DMA ERROR count="); Log_Number(errors); Log_Text("\r\n");
    }
    /* 50 帧/秒刷新。黑帧发送成功前一直保持 dirty，绝不遗漏关灯。 */
    if (ready && (dirty || (power_on && now-frame_at >= 20U)))
    {
        elapsed = now-frame_at;
        if (elapsed > 100U) elapsed = 100U;
        if (power_on) Effects_Render(mode, speed, elapsed, pixels);
        if (LED_Show(pixels, power_on ? brightness : 0U))
        {
            dirty = 0U;
            frame_at = now;
        }
    }
    /* 噪声和坏帧只累计诊断，每秒最多汇报一次，避免刷屏。 */
    if (now-diagnostics_at >= 1000U)
    {
        diagnostics_at = now;
        beat_log_pending = 1U;
        TemperatureLog();
        AudioLog();
        errors = IR_ErrorCount();
        if (errors != ir_errors_seen)
        {
            ir_errors_seen = errors;
            Log_Text("IR INVALID/OVERFLOW count=");
            Log_Number(errors); Log_Text("\r\n");
        }
    }
    /* 错开半秒输出，避免温度、音频和鼓点诊断同时挤满256字节缓冲。 */
    if (beat_log_pending && now-diagnostics_at >= 500U)
    {
        beat_log_pending = 0U;
        BeatLog();
    }
    Log_Task();
}

#include "effects.h"
#include "led_layout.h"
#include "temperature.h"
#include "audio.h"
#include "beat.h"

static uint32_t phase;
static uint32_t random_state = 0x713AC925U;
typedef struct { uint16_t pixel, age, duration; } Star;
/* 星光和火焰互斥，共用216 B工作区；切模式时清空，星光按需初始化。 */
static union {
    Star stars[8];
    uint8_t heat[LAYOUT_HEIGHT][LAYOUT_WIDTH];
} workspace;
static uint8_t stars_ready;
static uint32_t fire_time;
static uint8_t volume_on[LAYOUT_HEIGHT];
static uint8_t volume_fill[LAYOUT_HEIGHT];
#if VOLUME_HYSTERESIS_PERCENT == 0 || VOLUME_HYSTERESIS_PERCENT >= 100
#error VOLUME_HYSTERESIS_PERCENT must be between 1 and 99
#endif
#if VOLUME_FADE_MS == 0 || VOLUME_HOLD_BRIGHTNESS > 255 || VOLUME_HOLD_BRIGHTNESS == 0
#error Invalid volume fade or hold brightness
#endif
static const LED_Color colors[7] = {
    {255,0,0}, {255,96,0}, {255,255,0}, {0,255,0},
    {0,0,255}, {75,0,130}, {180,0,255}
};

static uint32_t Random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

/* 整数平滑插值，避免引入浮点和三角函数库。输入/输出均为 0..255。 */
static uint32_t Smooth(uint32_t x)
{
    return x * x * (765U - 2U * x) / 65025U;
}

static LED_Color Scale(LED_Color c, uint32_t level)
{
    c.r = (uint8_t)(c.r * level / 255U);
    c.g = (uint8_t)(c.g * level / 255U);
    c.b = (uint8_t)(c.b * level / 255U);
    return c;
}

static LED_Color Blend(LED_Color a, LED_Color b, uint32_t t)
{
    LED_Color c;
    c.r = (uint8_t)((a.r * (255U-t) + b.r*t) / 255U);
    c.g = (uint8_t)((a.g * (255U-t) + b.g*t) / 255U);
    c.b = (uint8_t)((a.b * (255U-t) + b.b*t) / 255U);
    return c;
}

static LED_Color Rainbow(uint32_t hue)
{
    static const LED_Color wheel[6] = {
        {255,0,0}, {255,255,0}, {0,255,0},
        {0,255,255}, {0,0,255}, {255,0,255}
    };
    hue %= 1536U;
    return Blend(wheel[hue / 256U], wheel[(hue / 256U + 1U) % 6U], hue % 256U);
}

void Effects_Reset(void)
{
    uint32_t x, y;
    phase = 0U;
    Beat_ClearPulse();
    fire_time = 0U;
    for (y = 0U; y < LAYOUT_HEIGHT; y++)
    {
        volume_on[y] = 0U;
        volume_fill[y] = 0U;
    }
    for (y = 0U; y < LAYOUT_HEIGHT; y++)
        for (x = 0U; x < LAYOUT_WIDTH; x++) workspace.heat[y][x] = 0U;
    stars_ready = 0U;
}

static void FireStep(void)
{
    uint32_t x, y, left, right, value, cooling;
    /* 从顶往下更新，读取的下方行仍是上一时刻，不需第二份热场。 */
    for (y = LAYOUT_HEIGHT-1U; y > 0U; y--)
    {
        for (x = 0U; x < LAYOUT_WIDTH; x++)
        {
            left = x == 0U ? x : x-1U;
            right = x+1U == LAYOUT_WIDTH ? x : x+1U;
            value = (2U*workspace.heat[y-1U][x] + workspace.heat[y-1U][left] + workspace.heat[y-1U][right])/4U;
            cooling = 8U + Random()%20U;
            workspace.heat[y][x] = (uint8_t)(value > cooling ? value-cooling : 0U);
        }
    }
    /* 底部补充热量，随机明暗形成向上升腾的火舌。 */
    for (x = 0U; x < LAYOUT_WIDTH; x++)
    {
        value = Random()%100U;
        workspace.heat[0][x] = (uint8_t)(value < 18U ? 70U+Random()%70U : 190U+Random()%66U);
    }
}

static LED_Color FireColor(uint8_t value)
{
    LED_Color c;
    /* 黑 -> 红 -> 橙黄 -> 淡黄白，最后仍受全局亮度上限约束。 */
    if (value < 85U) { c.r = value*3U; c.g = 0U; c.b = 0U; }
    else if (value < 170U) { c.r = 255U; c.g = (value-85U)*3U; c.b = 0U; }
    else { c.r = 255U; c.g = 255U; c.b = (value-170U)*2U; }
    return c;
}

static LED_Color TemperatureColor(void)
{
    static const LED_Color blue = {0,0,255}, green = {0,255,0}, red = {255,0,0};
    static const LED_Color purple = {48,0,64}, black = {0,0,0};
    int32_t raw;
    if (Temperature_IsFault()) return purple;
    if (!Temperature_HasSample()) return black;
    raw = Temperature_FilteredRaw();
    if (raw <= 18*16) return blue;
    if (raw >= 32*16) return red;
    if (raw <= 25*16) return Blend(blue, green, (uint32_t)(raw-18*16)*255U/(7U*16U));
    return Blend(green, red, (uint32_t)(raw-25*16)*255U/(7U*16U));
}

static void UpdateVolume(uint8_t level, uint32_t elapsed_ms)
{
    uint32_t y, base, off, target, height = (uint32_t)level*LAYOUT_HEIGHT;
    uint32_t band = (255U*VOLUME_HYSTERESIS_PERCENT+99U)/100U;
    uint32_t step = elapsed_ms >= VOLUME_FADE_MS ? 255U : elapsed_ms*255U/VOLUME_FADE_MS;
    if (elapsed_ms && !step) step = 1U;
    for (y = 0U; y < LAYOUT_HEIGHT; y++)
    {
        /* Audio_GetLevel在静音或采样故障时为0，强制清空状态防止残亮。 */
        if (!level) { volume_on[y] = 0U; volume_fill[y] = 0U; continue; }
        base = y*255U;
        off = base > band ? base-band : 0U;
        if (!volume_on[y] && height > base) volume_on[y] = 1U;
        else if (volume_on[y] && height <= off) volume_on[y] = 0U;
        target = 0U;
        if (volume_on[y])
        {
            target = height > base ? height-base : 0U;
            /* 滞回区保留可见亮度，避免亮灭状态未变但RGB又反复归零。 */
            if (target < VOLUME_HOLD_BRIGHTNESS) target = VOLUME_HOLD_BRIGHTNESS;
            if (target > 255U) target = 255U;
        }
        /* 按时间限速渐变，减少进入/离开滞回区时的亮度突变。 */
        if (target > volume_fill[y])
            volume_fill[y] += (uint8_t)(target-volume_fill[y] > step ? step : target-volume_fill[y]);
        else
            volume_fill[y] -= (uint8_t)(volume_fill[y]-target > step ? step : volume_fill[y]-target);
    }
}

void Effects_Render(uint8_t mode, uint8_t speed, uint32_t elapsed_ms,
                    LED_Color *pixels)
{
    /* 五档速度为 0.5、0.75、1、1.5、2 倍；phase 单位为 1/4 ms。 */
    static const uint8_t rates[5] = {2,3,4,6,8};
    static const LED_Color warm = {255,96,12};
    static const LED_Color star = {255,210,140};
    uint32_t i, p, t, distance;
    LED_Color c = {0,0,0};
    if (speed > 4U) speed = 4U;
    if (mode == 6U)
    {
        fire_time += elapsed_ms * rates[speed];
        /* 中档每 40 ms 推进一步；余量保留，调速不重置火焰。 */
        while (fire_time >= 160U) { FireStep(); fire_time -= 160U; }
        for (i = 0U; i < LED_COUNT; i++)
            pixels[i] = FireColor(workspace.heat[led_positions[i].y][led_positions[i].x]);
        return;
    }
    if (mode == 7U)
    {
        c = TemperatureColor();
        for (i = 0U; i < LED_COUNT; i++) pixels[i] = c;
        return;
    }
    if (mode == 8U)
    {
        UpdateVolume(Audio_GetLevel(), elapsed_ms);
        for (i = 0U; i < LED_COUNT; i++)
        {
            uint32_t y = led_positions[i].y;
            t = y*510U/(LAYOUT_HEIGHT-1U);
            c.r = (uint8_t)(t < 255U ? t : 255U);
            c.g = (uint8_t)(t < 255U ? 255U : 510U-t);
            c.b = 0U;
            pixels[i] = Scale(c, volume_fill[y]);
        }
        return;
    }
    if (mode == 9U)
    {
        /* 色彩12秒循环；亮度由鼓点脉冲决定，与原始音量波动解耦。 */
        phase = (phase+elapsed_ms)%12000U;
        c = Scale(Rainbow(phase*1536U/12000U), Audio_GetPulseLevel());
        for (i = 0U; i < LED_COUNT; i++) pixels[i] = c;
        return;
    }
    phase += elapsed_ms * rates[speed];
    if (mode == 1U)
    {
        phase %= 56000U;
        p = phase / 8000U;
        t = phase % 8000U;
        c = colors[p];
        if (t >= 4000U)
            c = Blend(c, colors[(p+1U)%7U], Smooth((t-4000U)*255U/4000U));
    }
    else if (mode == 2U)
    {
        phase %= 12000U;
        t = phase < 6000U ? phase : 12000U-phase;
        c = Scale(warm, Smooth(t*255U/6000U));
    }
    else if (mode == 3U) phase %= 16000U;
    else if (mode == 4U) phase %= (LED_COUNT+12U)*320U;
    else phase = 0U;

    for (i = 0U; i < LED_COUNT; i++)
    {
        if (mode == 3U)
        {
            /* 减去时间相位，使颜色沿灯珠编号增大的方向移动。 */
            c = Rainbow(i*1536U/LED_COUNT + 1536U - phase*1536U/16000U);
        }
        else if (mode == 4U)
        {
            p = phase / 320U;
            t = phase % 320U;
            c.r = c.g = c.b = 0U;
            if (i == p+1U)
            {
                c.r = (uint8_t)(180U*t/320U);
                c.g = (uint8_t)(220U*t/320U);
                c.b = (uint8_t)(255U*t/320U);
            }
            else if (i <= p && p-i < 12U)
            {
                distance = (p-i)*320U+t;
                c.r = 180U; c.g = 220U; c.b = 255U;
                c = Scale(c, (3840U-distance)*255U/3840U);
            }
        }
        else if (mode == 5U)
        {
            c.r = c.g = c.b = 0U;
        }
        pixels[i] = c;
    }
    if (mode == 5U)
    {
        if (!stars_ready)
        {
            for (i = 0U; i < 8U; i++)
            {
                workspace.stars[i].pixel = (uint16_t)(Random()%LED_COUNT);
                workspace.stars[i].duration = (uint16_t)(2400U+Random()%4000U);
                workspace.stars[i].age = 0U;
            }
            stars_ready = 1U;
        }
        /* 八颗星独立淡入淡出；熄灭后再随机选择位置，不会突然跳变。 */
        for (i = 0U; i < 8U; i++)
        {
            Star *s = &workspace.stars[i];
            p = s->age + elapsed_ms*rates[speed];
            if (p >= s->duration)
            {
                s->pixel = (uint16_t)(Random()%LED_COUNT);
                s->duration = (uint16_t)(2400U+Random()%4000U);
                p = 0U;
            }
            s->age = (uint16_t)p;
            t = p*510U/s->duration;
            if (t > 255U) t = 510U-t;
            c = Scale(star, Smooth(t));
            if (c.r > pixels[s->pixel].r) pixels[s->pixel] = c;
        }
    }
}

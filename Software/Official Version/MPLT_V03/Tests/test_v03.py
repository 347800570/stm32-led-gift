"""V03 tests: run compiled ARM audio and effects using deterministic ADC blocks.

Requires the same Unicorn/pyelftools environment as test_firmware.py.
Hardware acquisition, bus timing and interrupt preemption require board testing.
"""
import math
import struct
from test_firmware import Firmware
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2


class AudioDevice:
    def __init__(self):
        self.f = Firmware(mock_led=True, mock_audio=False)
        self.order = []
        self.fail_start = False
        self.buffer = None
        self.half = 0
        self.f.stub('HAL_TIM_Base_Stop', lambda: self.order.append('stop timer'))
        self.f.stub('HAL_ADC_Stop_DMA', lambda: self.order.append('stop adc'))
        self.f.stub('HAL_ADCEx_Calibration_Start', lambda: self.order.append('calibrate'))
        self.f.stub('HAL_ADC_Start_DMA', self.start_adc)
        self.f.stub('HAL_TIM_Base_Start', lambda: self.order.append('start timer'))
        self.f.call('Audio_Init')
        assert self.order[-3:] == ['calibrate', 'start adc', 'start timer']

    def start_adc(self):
        self.order.append('start adc')
        if self.fail_start:
            return 1
        self.buffer = self.f.uc.reg_read(UC_ARM_REG_R1)
        assert self.f.uc.reg_read(UC_ARM_REG_R2) == 128
        self.f.write32(0x40012400, 0)  # emulate clearing W1C ADC flags
        self.f.write32(0x4002000C, 128)
        self.half = 0
        return 0

    def block(self, values):
        assert len(values) == 64
        self.f.us += 8000
        self.f.uc.mem_write(self.buffer+self.half*128, struct.pack('<64H', *values))
        self.f.write32(0x4002000C, 64 if self.half == 0 else 128)
        callback = 'HAL_ADC_ConvHalfCpltCallback' if self.half == 0 else 'HAL_ADC_ConvCpltCallback'
        self.f.call(callback, self.f.symbols['hadc'])
        self.half ^= 1
        return self.f.call('Audio_GetLevel')

    def sine(self, peak, bias=2048):
        return self.block([round(bias+peak*math.sin(i*2*math.pi/16)) for i in range(64)])


def test_audio():
    d = AudioDevice()
    assert d.f.call('Audio_GetStatus') == 0
    assert d.block([2048]*64) == 0
    assert d.f.call('Audio_GetStatus') == 1, 'quiet stream is healthy'
    for _ in range(20):
        assert d.sine(4) == 0, 'fixed noise gate must reject small signals'
    for _ in range(20):
        low = d.sine(40)
    for _ in range(20):
        high = d.sine(160)
    assert 0 < low < high <= 255
    before = high
    after = d.block([2048]*64)
    assert 0 < after < before, 'release must decay rather than cut immediately'
    for _ in range(100):
        after = d.block([2048]*64)
    assert after == 0, 'silence must ultimately become fully black'
    d = AudioDevice()
    assert d.block([3000]*64) == 0, 'DC bias alone is not audio'
    assert d.f.call('Audio_GetBias') == 3000
    levels = []
    for sensitivity in range(5):
        d = AudioDevice()
        d.f.call('Audio_SetSensitivity', sensitivity)
        for _ in range(40):
            level = d.sine(80)
        levels.append(level)
    assert levels == sorted(levels) and len(set(levels)) == 5, levels
    d.block([0, 4095]*32)
    assert d.f.call('Audio_GetClippedSamples') == 64
    assert d.f.call('Audio_GetErrorCount') == 0
    print('PASS: startup order, quiet/DC rejection, noise gate, envelope, sensitivity, clipping diagnostics')


def test_pulse_release():
    d = AudioDevice()
    old8 = 0
    for peak in [160]*40 + [0]*100:
        value8 = d.sine(peak)
        target = min(255, max(0, d.f.call('Audio_GetAmplitude')-12)*255//128)
        next8 = old8+(target-old8+1)//2 if target > old8 else old8-(old8-target+15)//16
        assert value8 == next8, 'mode 8 keeps the original envelope'
        old8 = value8
    assert old8 == 0
    d.sine(160)
    d.f.call('HAL_ADC_ErrorCallback', d.f.symbols['hadc'])
    assert d.f.call('Audio_GetPulseLevel') == d.f.call('Audio_GetLevel') == 0
    f = Firmware()
    f.stub('Audio_GetLevel', lambda: 255)
    f.stub('Audio_GetPulseLevel', lambda: 0)
    f.call('Effects_Reset')
    f.call('Effects_Render', 9, 2, 20, 0x20003000)
    assert not any(f.uc.mem_read(0x20003000,201))
    print('PASS: unchanged mode-8 decay, fault blackout and independent mode-9 rendering route')


def test_audio_recovery():
    d = AudioDevice()
    d.sine(200)
    # Emulate ADC interrupt publishing a newer timestamp as HAL_GetTick returns.
    def interrupted_tick():
        old = d.f.us//1000
        d.f.write32(d.f.symbols['last_block_at'], old+1)
        d.f.us += 1000
        return old
    d.f.stub('HAL_GetTick', interrupted_tick)
    assert d.f.call('Audio_GetStatus') == 1
    d.f.call('Audio_Task')
    assert d.f.call('Audio_GetErrorCount') == 0, 'timestamp race is not a stale stream'
    d.f.stub('HAL_GetTick', lambda: d.f.us//1000)
    d.f.us += 101000
    assert d.f.call('Audio_GetStatus') == 2
    assert d.f.call('Audio_GetLevel') == 0
    d.f.call('Audio_Task')
    assert d.f.call('Audio_GetErrorCount') == 1
    d.f.us += 999000
    before = len(d.order)
    d.f.call('Audio_Task')
    assert len(d.order) == before
    d.f.us += 1000
    d.f.call('Audio_Task')
    assert d.f.call('Audio_GetStatus') == 0
    d.sine(200)
    assert d.f.call('Audio_GetStatus') == 1
    d.f.call('HAL_ADC_ErrorCallback', d.f.symbols['hadc'])
    assert d.f.call('Audio_GetLevel') == 0
    d.f.call('Audio_Task')
    d.fail_start = True
    d.f.us += 1000000
    d.f.call('Audio_Task')
    assert d.f.call('Audio_GetStatus') == 2
    d.fail_start = False
    d.f.us += 1000000
    d.f.call('Audio_Task')
    d.sine(200)
    assert d.f.call('Audio_GetStatus') == 1
    # DMA is writing the first half: a delayed first-half callback is unsafe.
    d.f.write32(0x4002000C, 120)
    d.f.call('HAL_ADC_ConvHalfCpltCallback', d.f.symbols['hadc'])
    assert d.f.call('Audio_GetStatus') == 2
    print('PASS: stale stream, DMA error, retry interval, restart failure/recovery, overwritten half rejection')


def test_sound_effects():
    f = Firmware()
    level = 0
    f.stub('Audio_GetLevel', lambda: level)
    f.stub('Audio_GetPulseLevel', lambda: level)
    target = 0x20003000
    coords = bytes(f.uc.mem_read(f.symbols['led_positions'], 134))
    heights = coords[1::2]
    for mode in [8, 9]:
        f.call('Effects_Reset')
        f.call('Effects_Render', mode, 2, 20, target)
        assert not any(f.uc.mem_read(target, 201)), 'silent output must be black'
    level = 128
    for _ in range(5):
        f.call('Effects_Render', 8, 2, 20, target)
    pixels = bytes(f.uc.mem_read(target, 201))
    for i, y in enumerate(heights):
        if y > 4:
            assert not any(pixels[i*3:i*3+3])
        elif y == 4:
            assert 0 < max(pixels[i*3:i*3+3]) < 100, 'boundary row must retain visible brightness'
        elif y == 0:
            assert pixels[i*3:i*3+3] == bytes([0,255,0])
    level = 255
    for _ in range(5):
        f.call('Effects_Render', 8, 2, 20, target)
    pixels = bytes(f.uc.mem_read(target, 201))
    for i, y in enumerate(heights):
        if y == 7:
            assert pixels[i*3:i*3+3] == bytes([255,0,0])
    f.call('Effects_Reset')
    f.call('Effects_Render', 9, 2, 0, target)
    a = bytes(f.uc.mem_read(target, 201))
    f.call('Effects_Render', 9, 2, 4000, target)
    b = bytes(f.uc.mem_read(target, 201))
    assert a == a[:3]*67 and b == b[:3]*67 and a != b
    # Regression for the shared star/fire workspace: entering fire must erase stars.
    f.call('Effects_Reset')
    f.call('Effects_Render', 5, 2, 200, target)
    f.call('Effects_Reset')
    f.call('Effects_Render', 6, 2, 0, target)
    assert not any(f.uc.mem_read(target, 201))
    print('PASS: height-based volume colors/fade, silence, whole-strip audio hue, shared workspace reset')


def test_volume_hysteresis():
    f = Firmware()
    level = 0
    target = 0x20003000
    f.stub('Audio_GetLevel', lambda: level)
    coords = bytes(f.uc.mem_read(f.symbols['led_positions'], 134))
    row = [i for i, y in enumerate(coords[1::2]) if y == 4]

    def render(value, frames=1):
        nonlocal level
        level = value
        for _ in range(frames):
            f.call('Effects_Render', 8, 2, 20, target)
        data = bytes(f.uc.mem_read(target, 201))
        colors = [data[i*3:i*3+3] for i in row]
        assert len(set(colors)) == 1, 'same height must have identical hysteresis'
        return max(colors[0])

    f.call('Effects_Reset')
    assert render(127, 5) == 0
    rising = [render(128) for _ in range(5)]
    assert rising[0] < rising[-1] and rising[-1] == 64
    # Row y=4 turns on at LEVEL=128 and off at LEVEL<=119 (25% band).
    for value in [127,128,126,129,120,125,128]*10:
        visible = render(value)
        assert visible >= 64
        assert visible*3//100 > 0, 'dominant channel stays visible even at 3% global brightness'
    assert render(119, 5) == 0
    for value in [120,125,127]*5:
        assert render(value) == 0, 'inside band an extinguished row stays off'
    assert render(128, 5) > 0
    assert render(0) == 0 and not any(f.uc.mem_read(target,201))
    assert render(125,5) == 0, 'silence clears latch'
    render(128,5)
    f.call('Effects_Reset')
    assert render(125,5) == 0, 're-entering mode clears latch'
    render(255,5)
    assert render(0) == 0, 'zero from acquisition fault cannot leave latched pixels'
    print('PASS: per-height hysteresis, noisy threshold, visible hold, fade, silence and re-entry reset')


def test_sound_keys():
    f = Firmware(mock_led=True)
    f.call('App_Init')

    def key(code):
        f.key(code)
        f.call('App_Task')
        return f.logs.splitlines()[-1]

    assert 'MODE=8' in key(0x52)
    for _ in range(10):
        state = key(0x09)
    assert 'SPEED=3 SENS=5' in state
    assert 'MODE=9' in key(0x4A) and 'SENS=5' in f.logs.splitlines()[-1]
    for _ in range(10):
        state = key(0x07)
    assert 'SPEED=3 SENS=1' in state
    key(0x5E)
    assert 'SPEED=4 SENS=1' in key(0x09), 'non-audio mode controls speed'
    key(0x45)
    for code in [0x52,0x4A,0x09,0x40]:
        assert key(code) == 'IGNORED: OFF'
    assert 'MODE=3 BRIGHTNESS=15% SPEED=4 SENS=1' in key(0x45)
    print('PASS: keys 8/9, independent shared sensitivity, limits, retained settings, off-state rules')


if __name__ == '__main__':
    test_audio()
    test_pulse_release()
    test_audio_recovery()
    test_sound_effects()
    test_volume_hysteresis()
    test_sound_keys()

"""Feed synthetic microphone signals into the compiled ARM firmware.

This tests the lightweight onset detector, not recognition of real music.
"""
import math
import random
import struct
from test_v03 import AudioDevice


class Stream:
    def __init__(self, sensitivity=2):
        self.d = AudioDevice()
        self.d.f.call('Audio_SetSensitivity', sensitivity)
        self.n = 0
        self.events = []
        self.count = 0

    def feed(self, blocks, wave):
        for _ in range(blocks):
            values = [max(0,min(4095,round(2048+wave((self.n+i)/8000)))) for i in range(64)]
            self.d.block(values)
            self.n += 64
            count = self.d.f.call('Beat_Count')
            if count != self.count:
                self.events.append((self.n/8000, self.d.f.call('Audio_GetPulseLevel')))
                self.count = count


def kick(t, starts, amplitude=320):
    result = 0
    for start in starts:
        age = t-start
        if 0 <= age < .12:
            result += amplitude*math.exp(-age/.035)*math.sin(2*math.pi*90*age)
    return result


def test_beats():
    noise = random.Random(921)
    s = Stream()
    s.feed(250, lambda t: noise.uniform(-12,12))
    assert s.count == 0, s.events
    s.feed(250, lambda t: 160*math.sin(2*math.pi*1500*t))
    assert s.count == 0, 'high-frequency signal should not act like a kick'
    s = Stream()
    starts = [.5,1.,1.5,2.]
    s.feed(313, lambda t: kick(t,starts))
    assert s.count == len(starts), s.events
    assert all(0 <= event[0]-start < .08 for event,start in zip(s.events,starts)), s.events
    assert all(event[1] == 255 for event in s.events), 'fixed peak for every detected beat'
    assert s.d.f.call('Audio_GetPulseLevel') == 0
    # Sustained low-frequency tone can trigger at onset but must not keep flashing.
    s = Stream()
    s.feed(500, lambda t: 200*math.sin(2*math.pi*90*(t-.5)) if t >= .5 else 0)
    assert s.count == 1, s.events
    print('PASS: quiet noise/high-frequency rejection, four kicks, fixed peaks and sustained-tone rejection')


def test_pulse_and_lockout():
    s = Stream()
    s.feed(63, lambda t: 0)
    for _ in range(12):
        s.feed(1, lambda t: kick(t,[.504]))
        if s.count:
            break
    assert s.count == 1
    f = s.d.f
    start = f.us//1000
    for elapsed in [0,20,50,100,149,150,200]:
        f.us = (start+elapsed)*1000
        assert f.call('Beat_PulseLevel') == (max(0,150-elapsed)*255//150)
    assert f.call('Beat_Count') == 1
    s = Stream()
    s.feed(190, lambda t: kick(t,[.5,.58,.9,1.02,1.3]))
    assert s.count >= 3, s.events
    assert all(b[0]-a[0] >= .15 for a,b in zip(s.events,s.events[1:])), s.events
    f = s.d.f
    f.call('Effects_Reset')
    assert f.call('Beat_PulseLevel') == 0
    f.call('HAL_ADC_ErrorCallback', f.symbols['hadc'])
    assert f.call('Audio_GetPulseLevel') == 0
    print('PASS: independent 150 ms fade, minimum trigger spacing, mode-reset and fault blackout')


def test_sensitivity():
    counts = []
    for index in range(5):
        s = Stream(index)
        s.feed(125, lambda t: kick(t,[.5],100))
        counts.append(s.count)
    assert counts == sorted(counts) and counts[0] == 0 and counts[-1] == 1, counts
    print('PASS: sensitivity makes weak kick detection easier without changing pulse peak')


def test_diagnostics():
    s = Stream()
    f = s.d.f
    peak = -1
    expected = None
    for block in range(125):
        bg = f.call('Beat_Background')
        s.feed(1, lambda t: kick(t, [.5]))
        energy = f.call('Beat_Energy')
        if block >= 16 and energy > peak:
            peak = energy
            expected = (peak, bg*200//100+64, bg)
    ptr = 0x20003000
    f.call('Beat_TakeDiagnostics', ptr)
    values = struct.unpack('<3I5H2B', f.uc.mem_read(ptr, 24))
    assert values[:3] == expected, (values, expected)
    assert sum(values[3:8]) == 109, values
    assert values[3] == s.count == 1 and values[8:] == (3, 1), values
    f.call('Beat_TakeDiagnostics', ptr)
    assert bytes(f.uc.mem_read(ptr, 24)) == bytes(24)
    s.feed(1, lambda t: 0)
    f.call('Beat_TakeDiagnostics', ptr)
    values = struct.unpack('<3I5H2B', f.uc.mem_read(ptr, 24))
    assert sum(values[3:8]) == 1 and values[-1] == 1, values
    # Moderate 300 Hz transients should now be admitted, not only deep bass.
    s = Stream()
    s.feed(125, lambda t: 150*math.exp(-(t-.5)/.035)*math.sin(2*math.pi*300*(t-.5))
           if .5 <= t < .62 else 0)
    assert s.count == 1, s.events
    print('PASS: paired peak/threshold, exclusive window counters, snapshot reset and 300 Hz onset')


if __name__ == '__main__':
    test_beats()
    test_pulse_and_lockout()
    test_sensitivity()
    test_diagnostics()

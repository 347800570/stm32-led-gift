"""V02 regression tests on compiled ARM functions, with a scripted 1-Wire device.

Run test_firmware.py first, then this script. Electrical timing is not simulated.
"""
from test_firmware import Firmware, ROOT
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_PRIMASK


def crc8(data):
    crc = 0
    for byte in data:
        for _ in range(8):
            mix = (crc ^ byte) & 1
            crc >>= 1
            if mix:
                crc ^= 0x8C
            byte >>= 1
    return crc


class Sensor:
    def __init__(self):
        self.f = Firmware(mock_led=True, mock_temperature=False)
        self.present = True
        self.ready = True
        self.commands = []
        self.reply = []
        self.reading = False
        self.f.stub('ResetBus', lambda: int(self.present))
        self.f.stub('ReadBit', self.read_bit)
        self.f.stub('WriteByte', self.write_byte)
        self.f.call('Temperature_Init')

    def write_byte(self):
        byte = self.f.uc.reg_read(UC_ARM_REG_R0)
        self.commands.append(byte)
        if byte == 0xBE:
            self.reading = True

    def read_bit(self):
        if not self.reading:
            return int(self.ready)
        bit = self.reply.pop(0)
        if not self.reply:
            self.reading = False
        return bit

    def sample(self, raw, config=0x7F, corrupt=False, expect_sample=True):
        before = self.f.call('Temperature_SampleCount')
        self.f.us += 1000000
        self.f.call('Temperature_Task')
        self.f.us += 759000
        self.f.call('Temperature_Task')
        assert self.f.call('Temperature_SampleCount') == before
        self.f.us += 1000
        self.reply = [raw & 255, (raw >> 8) & 255, 75, 70, config, 255, 12, 16]
        self.reply.append(crc8(self.reply) ^ int(corrupt))
        self.reply = [(byte >> bit) & 1 for byte in self.reply for bit in range(8)]
        self.f.call('Temperature_Task')
        for _ in range(9):
            self.f.call('Temperature_Task')
        assert not self.reply
        assert self.f.call('Temperature_SampleCount') == before + int(expect_sample)


def render_temperature(f):
    f.call('Effects_Render', 7, 2, 20, 0x20003000)
    output = bytes(f.uc.mem_read(0x20003000, 222))
    assert output == output[:3]*74
    return output[:3]


def test_temperature():
    for degrees, color in [(-10, (0, 0, 255)), (18, (0, 0, 255)),
                           (25, (0, 255, 0)), (32, (255, 0, 0)),
                           (85, (255, 0, 0))]:
        s = Sensor()
        s.sample(degrees*16)
        assert render_temperature(s.f) == bytes(color)
        assert s.commands == [0xCC, 0x44, 0xCC, 0xBE]
    s = Sensor()
    assert render_temperature(s.f) == bytes(3), 'no reading is not 0 C'
    s.sample(25*16)
    s.sample(32*16)
    assert 25*16 < s.f.call('Temperature_FilteredRaw') < 32*16
    s.sample(32*16, corrupt=True, expect_sample=False)
    assert s.f.call('Temperature_GetStatus') == 3
    saved = render_temperature(s.f)
    s.present = False
    s.f.us += 1000000
    s.f.call('Temperature_Task')
    assert s.f.call('Temperature_GetStatus') == 2
    assert render_temperature(s.f) == saved, 'brief error must keep last color'
    s.f.us += 5000000
    s.f.call('Temperature_Task')
    assert render_temperature(s.f) == bytes([48, 0, 64])
    s.present = True
    s.sample(18*16)
    assert render_temperature(s.f) == bytes([0, 0, 255]), 'recovery must clear fault'
    s.sample(126*16, expect_sample=False)
    assert s.f.call('Temperature_GetStatus') == 4
    s.sample(0, config=0, expect_sample=False)
    assert s.f.call('Temperature_GetStatus') == 4
    s = Sensor()
    s.sample(-161, config=0x1F)
    assert s.f.call('Temperature_LatestRaw') & 0xFFFF == (-168 & 0xFFFF)
    s = Sensor()
    s.ready = False
    s.f.call('Temperature_Task')
    s.f.us += 760000
    s.f.call('Temperature_Task')
    assert s.f.call('Temperature_GetStatus') == 5
    print('PASS: temperature mapping, signed/resolution data, CRC, smoothing, failure and recovery')


def test_wire_critical_sections():
    f = Firmware()
    masked = []

    def delay():
        us = f.uc.reg_read(UC_ARM_REG_R0)
        if f.uc.reg_read(UC_ARM_REG_PRIMASK):
            masked.append(us)
        f.us += us

    f.stub('DelayUs', delay)
    levels = iter([1, 0, 1])
    f.stub('BusHigh', lambda: next(levels))
    f.uc.reg_write(UC_ARM_REG_PRIMASK, 0)
    assert f.call('ResetBus') == 1
    assert masked == [70], 'reset 480 us must not mask IR interrupt'
    assert f.uc.reg_read(UC_ARM_REG_PRIMASK) == 0
    for byte, limit in [(0, 60), (255, 6)]:
        masked.clear()
        f.call('WriteByte', byte)
        assert masked == [limit]*8
        assert f.uc.reg_read(UC_ARM_REG_PRIMASK) == 0
    masked.clear()
    f.stub('BusHigh', lambda: 1)
    assert f.call('ReadBit') == 1
    assert sum(masked) == 13
    f.uc.reg_write(UC_ARM_REG_PRIMASK, 1)
    f.call('WriteByte', 255)
    assert f.uc.reg_read(UC_ARM_REG_PRIMASK) == 1, 'preserve caller mask'
    f.uc.reg_write(UC_ARM_REG_PRIMASK, 0)
    f.stub('BusHigh', lambda: 0)
    assert f.call('ResetBus') == 0, 'reject stuck low bus'
    print('PASS: short 1-Wire critical sections and PRIMASK restoration (instruction overhead excluded)')


def test_layout_fire():
    f = Firmware()
    grid = (ROOT / '灯带位置.txt').read_text(encoding='utf-8-sig').splitlines()
    expected = {}
    for row, line in enumerate(grid):
        for x, token in enumerate(line.split('\t')):
            if token.strip():
                number = int(token.strip())
                assert number not in expected
                expected[number] = (x, len(grid)-1-row)
    assert set(expected) == set(range(1, 75))
    positions = bytes(f.uc.mem_read(f.symbols['led_positions'], 148))
    actual = [tuple(positions[i:i+2]) for i in range(0, 148, 2)]
    assert actual == [expected[i] for i in range(1, 75)]
    f.call('Effects_Reset')
    output = 0x20003000
    f.call('Effects_Render', 6, 2, 40, output)
    frame = bytes(f.uc.mem_read(output, 222))
    lit = [i for i in range(74) if any(frame[i*3:i*3+3])]
    assert lit and all(actual[i][1] == 0 for i in lit), 'fire must start at board bottom'
    f.call('Effects_Render', 6, 2, 40, output)
    frame = bytes(f.uc.mem_read(output, 222))
    lit = [i for i in range(74) if any(frame[i*3:i*3+3])]
    assert any(actual[i][1] == 1 for i in lit)
    assert all(actual[i][1] <= 1 for i in lit)
    frames = []
    for _ in range(100):
        f.call('Effects_Render', 6, 2, 40, output)
        frames.append(bytes(f.uc.mem_read(output, 222)))
    assert len(set(frames)) > 80
    assert any(any(frames[-1][i*3:i*3+3]) for i in range(74) if actual[i][1] == 11)
    print('PASS: all 74 physical coordinates, fire starts low, rises in height, and animates')


def test_v02_keys():
    f = Firmware(mock_led=True)
    f.call('App_Init')
    for code, mode in [(0x5A, 6), (0x42, 7)]:
        f.key(code)
        f.call('App_Task')
        assert f'STATE ON MODE={mode} ' in f.logs.splitlines()[-1]
    f.key(0x45)
    f.call('App_Task')
    for code in [0x5A, 0x42]:
        f.key(code)
        f.call('App_Task')
        assert f.logs.endswith('IGNORED: OFF\r\n')
    f.key(0x45)
    f.call('App_Task')
    assert 'STATE ON MODE=7 ' in f.logs.splitlines()[-1]
    for code in [0x47, 0x16]:
        f.key(code)
        f.call('App_Task')
        assert f.logs.endswith('UNASSIGNED IN SRKL V01\r\n')
    print('PASS: keys 6/7 select modes, cannot wake, other unassigned keys ignored')


if __name__ == '__main__':
    test_temperature()
    test_wire_critical_sections()
    test_layout_fire()
    test_v02_keys()

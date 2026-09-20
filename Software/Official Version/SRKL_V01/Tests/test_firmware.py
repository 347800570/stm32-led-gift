"""Execute the compiled ARM functions with emulated HAL boundaries.

Requires unicorn and pyelftools. This validates logic, not electrical timing.
Run after a Keil build: python Tests/test_firmware.py
"""
from pathlib import Path
import struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_XPSR,
)

ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / 'MDK-ARM/MPLT/SRKL.axf'


class Firmware:
    def __init__(self, mock_led=False, mock_temperature=True, mock_audio=True):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in [(0x08000000, 0x10000), (0x20000000, 0x10000),
                              (0x40000000, 0x30000), (0x48000000, 0x1000)]:
            self.uc.mem_map(address, size)
        with IMAGE.open('rb') as stream:
            elf = ELFFile(stream)
            self.symbols = {s.name: s['st_value'] for s in
                            elf.get_section_by_name('.symtab').iter_symbols()}
            for seg in elf.iter_segments():
                if seg['p_type'] == 'PT_LOAD' and seg['p_filesz']:
                    self.uc.mem_write(seg['p_vaddr'], seg.data())
            # Calls bypass __main: reproduce scatter-loading at execution addresses,
            # particularly initialized RAM (the RNG seed and const pointer tables).
            for section in elf.iter_sections():
                if section['sh_flags'] & 2 and section['sh_type'] == 'SHT_PROGBITS':
                    self.uc.mem_write(section['sh_addr'], section.data())
        self.us = 0
        self.level = 1
        self.logs = ''
        self.frames = []
        self.transfers = []
        self.ready = True
        self.uc.reg_write(UC_ARM_REG_XPSR, 0x01000000)
        self.write32(self.symbols['htim14'], 0x40002000)
        self.write32(self.symbols['htim3'], 0x40000400)
        self.write32(self.symbols['htim1'], 0x40012C00)
        self.write32(self.symbols['hadc'], 0x40012400)
        self.write32(self.symbols['hdma_adc'], 0x40020008)
        # Verified ARMCC ABI: DMA_Handle is at byte 48 in this target's ADC handle.
        self.write32(self.symbols['hadc']+48, self.symbols['hdma_adc'])
        self.hooks = {}
        self.stub('HAL_GetTick', lambda: self.us // 1000)
        self.stub('HAL_TIM_Base_Start', lambda: 0)
        self.stub('HAL_GPIO_ReadPin', lambda: self.level)
        self.stub('Log_Task', lambda: 0)
        self.stub('Log_Text', self.text)
        self.stub('Log_Number', self.number)
        self.stub('Log_Hex8', self.hex8)
        self.stub('HAL_TIM_PWM_Start_DMA', self.start_dma)
        self.stub('HAL_TIM_PWM_Stop_DMA', lambda: 0)
        self.stub('TemperatureLog', lambda: 0)
        self.stub('AudioLog', lambda: 0)
        self.stub('BeatLog', lambda: 0)
        if mock_audio:
            self.stub('Audio_Init', lambda: 0)
            self.stub('Audio_Task', lambda: 0)
        if mock_temperature:
            self.stub('Temperature_Task', lambda: 0)
        if mock_led:
            self.stub('LED_Init', lambda: 0)
            self.stub('LED_Ready', lambda: int(self.ready))
            self.stub('LED_ErrorCount', lambda: 0)
            self.stub('LED_Show', self.show)
        self.uc.hook_add(UC_HOOK_CODE, self.dispatch)

    def write32(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value))

    def stub(self, name, callback):
        self.hooks[self.symbols[name] & ~1] = callback

    def dispatch(self, uc, address, size, data):
        callback = self.hooks.get(address)
        if callback:
            uc.reg_write(UC_ARM_REG_R0, callback() or 0)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def text(self):
        address = self.uc.reg_read(UC_ARM_REG_R0)
        value = bytearray()
        while True:
            c = self.uc.mem_read(address, 1)[0]
            if not c:
                break
            value.append(c)
            address += 1
        self.logs += value.decode('ascii')

    def number(self):
        self.logs += str(self.uc.reg_read(UC_ARM_REG_R0))

    def hex8(self):
        self.logs += f'{self.uc.reg_read(UC_ARM_REG_R0):02X}'

    def show(self):
        self.frames.append((self.uc.reg_read(UC_ARM_REG_R1), bytes(
            self.uc.mem_read(self.uc.reg_read(UC_ARM_REG_R0), 74*3))))
        return 1

    def start_dma(self):
        address = self.uc.reg_read(UC_ARM_REG_R2)
        length = self.uc.reg_read(UC_ARM_REG_R3)
        self.transfers.append(bytes(self.uc.mem_read(address, length)))
        return 0

    def call(self, name, *args):
        for reg, arg in zip([UC_ARM_REG_R0, UC_ARM_REG_R1,
                             UC_ARM_REG_R2, UC_ARM_REG_R3], args):
            self.uc.reg_write(reg, arg)
        self.uc.reg_write(UC_ARM_REG_SP, 0x20007FF0)
        self.uc.reg_write(UC_ARM_REG_LR, 0x0800F001)
        self.uc.emu_start(self.symbols[name] | 1, 0x0800F000, count=1000000)
        assert self.uc.reg_read(UC_ARM_REG_PC) == 0x0800F000, name + ' did not return'
        return self.uc.reg_read(UC_ARM_REG_R0)

    def edge(self, delay, level):
        self.us += delay
        self.level = level
        self.write32(0x40002024, self.us & 0xFFFF)
        self.call('HAL_GPIO_EXTI_Callback', 8)

    def key(self, code, repeat=False, valid=True, gap=30000):
        self.edge(gap, 0)
        self.edge(9000, 1)
        self.edge(2250 if repeat else 4500, 0)
        if not repeat:
            for byte in [0, 255, code, code ^ (255 if valid else 254)]:
                for bit in range(8):
                    self.edge(560, 1)
                    self.edge(1690 if byte & (1 << bit) else 560, 0)
        self.edge(560, 1)


def test_controls():
    f = Firmware(mock_led=True)
    f.call('App_Init')
    f.us = 20000
    f.call('App_Task')
    assert 'STATE ON MODE=3 BRIGHTNESS=15% SPEED=3' in f.logs
    for code, mode in [(0x0C, 1), (0x18, 2), (0x5E, 3), (0x08, 4), (0x1C, 5)]:
        f.key(code)
        f.call('App_Task')
        assert f'STATE ON MODE={mode} ' in f.logs.splitlines()[-1]
    f.ready = False
    f.key(0x45)
    f.call('App_Task')
    f.ready = True
    f.call('App_Task')
    assert f.frames[-1][0] == 0, 'off frame must wait for busy DMA'
    for code in [0x0C, 0x40, 0x09, 0x5A]:
        f.key(code)
        f.call('App_Task')
        assert f.logs.endswith('IGNORED: OFF\r\n')
    f.key(0x45)
    f.call('App_Task')
    assert f.logs.endswith('STATE ON MODE=5 BRIGHTNESS=15% SPEED=3 SENS=3\r\n')
    before = f.logs
    f.key(0x45, repeat=True)
    f.call('App_Task')
    assert f.logs == before, 'POWER repeat must not toggle'
    for code in [0x40]*12 + [0x09]*8:
        f.key(code)
        f.call('App_Task')
    assert f.logs.endswith('STATE ON MODE=5 BRIGHTNESS=30% SPEED=5 SENS=3\r\n')
    for code in [0x19]*12 + [0x07]*8:
        f.key(code)
        f.call('App_Task')
    assert f.logs.endswith('STATE ON MODE=5 BRIGHTNESS=3% SPEED=1 SENS=3\r\n')
    f.key(0x40)
    f.call('App_Task')
    for _ in range(6):
        f.key(0x40, repeat=True, gap=95000)
        f.call('App_Task')
    assert 'REPEAT + ' in f.logs
    before = f.logs
    f.key(0x40, repeat=True, gap=300000)
    f.call('App_Task')
    assert f.logs == before, 'stale repeats must be rejected'
    f.key(0x45, valid=False)
    f.call('App_Task')
    assert f.call('IR_ErrorCount') > 0
    assert f.logs.count('STATE OFF') == 1, 'bad frame must not toggle'
    print('PASS: five modes, off-state rules, busy shutdown, limits, hold, invalid NEC')


def test_effects():
    f = Firmware()
    address = 0x20003000
    for mode in range(1, 6):
        f.call('Effects_Reset')
        frames = []
        for _ in range(150):
            f.call('Effects_Render', mode, 2, 20, address)
            frames.append(bytes(f.uc.mem_read(address, 222)))
        assert len(set(frames)) > 10, mode
        if mode in (1, 2):
            assert all(frame == frame[:3]*74 for frame in frames)
    f.call('Effects_Reset')
    f.call('Effects_Render', 1, 2, 0, address)
    assert bytes(f.uc.mem_read(address, 222)) == bytes([255, 0, 0])*74
    f.call('Effects_Render', 1, 2, 1000, address)
    assert bytes(f.uc.mem_read(address, 3)) == bytes([255, 0, 0])
    f.call('Effects_Render', 1, 2, 1000, address)
    assert bytes(f.uc.mem_read(address, 3)) == bytes([255, 96, 0])
    print('PASS: effects animate, whole-strip uniformity, seven-color hold/transition')


def test_led():
    f = Firmware()
    address = 0x20003000
    f.uc.mem_write(address, bytes([255, 128, 64])*74)
    f.call('LED_Init')
    f.us = 2000
    assert f.call('LED_Show', address, 100) == 1
    data = f.transfers[-1]
    assert len(data) == 2034 and data[:2] == b'\0'*2 and data[-256:] == b'\0'*256
    expected = (38 << 16) | (76 << 8) | 19
    actual = 0
    for duty in data[2:26]:
        assert duty in (19, 38)
        actual = (actual << 1) | int(duty == 38)
    assert actual == expected, (actual, expected)
    assert f.call('LED_Show', address, 15) == 0, 'busy buffer must not be overwritten'
    f.us += 30000
    assert f.call('LED_Ready') == 0
    assert f.call('LED_ErrorCount') == 1
    f.us += 2000
    assert f.call('LED_Show', address, 0) == 1
    assert set(f.transfers[-1][2:-256]) == {19}, 'off must encode all zero data bits'
    print('PASS: GRB order, brightness cap, reset slots, busy protection, timeout recovery')


if __name__ == '__main__':
    test_controls()
    test_effects()
    test_led()

"""Run compiled firmware regressions after building both Keil projects."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
for name in ('MPLT_V03', 'SRKL_V01'):
    tests = root / 'Software/Official Version' / name / 'Tests'
    for test in ('test_firmware.py', 'test_v02.py', 'test_v03.py', 'test_beat.py'):
        subprocess.run([sys.executable, str(tests / test)], check=True)

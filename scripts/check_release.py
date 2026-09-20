"""Verify distributable firmware, project references and tracked-file hygiene."""
from pathlib import Path
import hashlib
import re
import subprocess
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
for line in (root/'firmware/SHA256SUMS').read_text().splitlines():
    digest, name = line.split('  ',1)
    data = (root/'firmware'/name).read_bytes()
    assert hashlib.sha256(data).hexdigest() == digest, name
    for record in data.decode('ascii').splitlines():
        assert record.startswith(':')
        raw = bytes.fromhex(record[1:])
        assert len(raw) == raw[0]+5 and sum(raw)%256 == 0, name
    assert data.decode('ascii').splitlines()[-1] == ':00000001FF'
for project in root.glob('Software/**/*.uvprojx'):
    tree = ET.parse(project)
    for item in tree.findall('.//FilePath'):
        name = (item.text or '').replace('\\','/')
        if name:
            assert not re.match(r'^[A-Za-z]:',name), (project,name)
            assert (project.parent/name).exists(), (project,name)
    for item in tree.findall('.//IncludePath'):
        for name in (item.text or '').replace('\\','/').split(';'):
            if name:
                assert (project.parent/name).is_dir(), (project,name)
tracked = subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
for name in filter(None,tracked):
    assert not any(part in name for part in ('现有方案调研','__pycache__','-backups')), name
    p = root/name
    if p.suffix in ('.c','.h','.py','.md','.ioc','.uvprojx','.kicad_pro','.kicad_sch','.kicad_pcb','.txt','.lst'):
        text = p.read_text(encoding='utf-8-sig',errors='replace')
        assert not re.search(r'[CD]:[/\\](?:Users|Experiment|AbokaLibraries)',text,re.I), name
        assert 'License '+'Information:' not in text, name
print('PASS: firmware hashes/HEX checksums, project references and tracked-file hygiene')

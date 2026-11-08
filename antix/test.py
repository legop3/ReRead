"""Optional short SSD boot/input check; no installer verification passes."""
import json
import time
from pathlib import Path
from vm import VM
BUILD=Path('/work/build/antix');OUT=Path('/work/out/antix')
m=json.loads((OUT/'manifest.json').read_text())
v=VM(['-drive',f'file={OUT / m["system_image"]},format=raw,if=ide,snapshot=on'],
     'boot-test',build=BUILD,out=OUT,memory=512)
try:
    v.expect('boot:');v.send('serial')
    v.expect('READER BASE: antiX Core',timeout=180)
    v.expect('Mapped: 19/19')
    v.monitor('sendkey up');v.expect('Code 103  UP');v.expect('RELEASED')
    for cycle in range(2):
        v.monitor('system_powerdown')
        v.expect('Sleeping... Press Power to wake.')
        end=time.monotonic()+30
        while 'suspended' not in v.monitor('info status'):
            if time.monotonic()>end:raise TimeoutError('VM did not suspend')
            time.sleep(.2)
        v.monitor('system_wakeup')
        v.expect('Awake. Power: press and release to sleep')
        time.sleep(1.1)
        assert 'running' in v.monitor('info status'), 'wake event caused another suspend'
    print('antiX SSD boot, input, and two power-button suspend/resume cycles passed.',flush=True)
finally:v.close()

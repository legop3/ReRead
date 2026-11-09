"""Compile and navigate Software against a disposable CLI inside the builder."""
from pathlib import Path
import json,os,subprocess
base=Path('/work/build/repository'); base.mkdir(parents=True,exist_ok=True)
env={**os.environ,'PKG_CONFIG_LIBDIR':'/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig'}
qt=subprocess.check_output(['pkg-config','--cflags','--libs','Qt5Widgets'],env=env,text=True).split()
subprocess.run(['g++','-m32','-std=c++17','-O2','-fPIC','-Wall','-Wextra','-Werror',
 '-I/work/antix/ui','-I/work/antix','/work/antix/apps/software/test-navigation.cpp',
 '-L/work/build/antix/ui','-lreader-ui','-lreader-device',*qt,'-o','/work/build/antix/ui/test-software'],check=True)
state=dict(version='0.13.0',apps=[dict(package='reread-app-camera',name='Camera',version='0.4.0'),dict(package='reread-app-software',name='Software',version='0.1.0')],job=dict(state='complete',message='Ready'),log='',catalog=dict(checked=1794250000,packages=[dict(package='reread',version='0.14.0',upgrade=True),dict(package='reread-app-notes',version='1.0',upgrade=False,description='Notes')]))
(base/'software-fixture.json').write_text(json.dumps(state))
(base/'software-actions.jsonl').write_text('')
fixture=base/'software-fixture.py'
fixture.write_text('''#!/usr/bin/python3
import json,sys
from pathlib import Path
base=Path('/work/build/repository'); p=base/'software-fixture.json'; state=json.loads(p.read_text())
action=sys.argv[1]; name=sys.argv[2] if len(sys.argv)>2 else ''
if action!='status':
 with (base/'software-actions.jsonl').open('a') as out: out.write(json.dumps([action,name])+'\\n')
 if action=='install' and name!='reread': state['apps'].append(dict(package=name,name='Notes',version='1.0'))
 if action=='remove': state['apps']=[a for a in state['apps'] if a['package']!=name]
 state['job']=dict(state='complete',message='Complete.');p.write_text(json.dumps(state))
 print(json.dumps(state['job']))
else: print(json.dumps(state))
''');fixture.chmod(0o755)
command=Path('/usr/local/sbin/reader-software'); assert not command.exists()
command.symlink_to(fixture)
try:
 subprocess.run(['/work/build/antix/ui/test-software'],check=True,env={**os.environ,'QT_QPA_PLATFORM':'offscreen','LD_LIBRARY_PATH':'/work/build/antix/ui'})
 actions=[json.loads(line) for line in (base/'software-actions.jsonl').read_text().splitlines()]
 assert actions==[['check',''],['install','reread-app-notes'],['remove','reread-app-notes'],['install','reread']],actions
 print('PASS: native button navigation reaches repository check, app install/removal confirmation, and system update')
finally: command.unlink()

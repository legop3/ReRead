"""Shared QEMU serial/monitor harness; no host disks exposed."""
import os
from pathlib import Path
import select
import socket
import subprocess
import time

class VM:
    def __init__(self,args,name,allow_reboot=False,build=Path('/work/build/antix'),out=Path('/work/out/antix'),memory=128):
        self.socket=build/(name+'.sock');self.socket.unlink(missing_ok=True)
        self.log=(out/(name+'.log')).open('wb')
        self.p=subprocess.Popen(['qemu-system-i386','-machine','accel=kvm:tcg','-cpu','qemu32',
            '-m',str(memory),'-display','none','-serial','stdio','-monitor',f'unix:{self.socket},server=on,wait=off',
            *([] if allow_reboot else ['-no-reboot']),*args],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        self.pending=b''
    def expect(self,value,timeout=120):
        token=value.encode();end=time.monotonic()+timeout
        while token not in self.pending:
            if time.monotonic()>end: raise TimeoutError(value)
            if select.select([self.p.stdout],[],[],1)[0]:
                chunk=os.read(self.p.stdout.fileno(),65536)
                if not chunk: raise RuntimeError('VM exited before '+value)
                self.log.write(chunk);self.log.flush();self.pending+=chunk
        self.pending=self.pending.split(token,1)[1]
    def send(self,value): self.p.stdin.write(value.encode()+b'\n');self.p.stdin.flush()
    def monitor(self,command):
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(3)
            s.connect(str(self.socket));s.recv(4096);s.sendall(command.encode()+b'\n');time.sleep(.5)
            return s.recv(65536).decode(errors='replace')
    def close(self):
        if self.p.poll() is None:self.p.terminate();self.p.wait(timeout=10)
        self.log.close()

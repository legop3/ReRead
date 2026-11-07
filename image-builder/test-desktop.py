"""Boot the finished disk through its BIOS bootloader and capture its screen."""
from pathlib import Path
import socket
import subprocess
import time

BASE = Path('/work')
OUT = BASE / 'out'
monitor = BASE / 'build/desktop-monitor.sock'
monitor.unlink(missing_ok=True)
subprocess.run([
    'qemu-system-i386', '-machine', 'accel=kvm:tcg', '-m', '512', '-cpu', 'qemu32',
    '-drive', f'file={OUT}/reader-workbench.img,format=raw,if=ide',
    '-vga', 'std', '-display', 'none', '-serial', f'file:{OUT}/desktop-serial.log',
    '-monitor', f'unix:{monitor},server=on,wait=off', '-no-reboot',
    '-daemonize', '-pidfile', str(BASE / 'build/desktop-vm.pid'),
], check=True)
print('Booting BIOS disk image with 512 MiB RAM...', flush=True)
for i in range(8 if Path('/dev/kvm').exists() else 20):
    time.sleep(15)
    print(f'Desktop boot: {(i + 1) * 15} seconds elapsed.', flush=True)
with socket.socket(socket.AF_UNIX) as connection:
    connection.connect(str(monitor))
    connection.recv(4096)
    connection.sendall(f'screendump {OUT}/desktop.ppm\n'.encode())
    time.sleep(1)
    print(connection.recv(4096).decode(errors='replace'), flush=True)
with socket.socket(socket.AF_UNIX) as connection:
    connection.connect(str(monitor))
    connection.recv(4096)
    connection.sendall(b'system_powerdown\n')
print('Screen captured; requesting orderly ACPI shutdown.', flush=True)
pid = int((BASE / 'build/desktop-vm.pid').read_text())
deadline = time.monotonic() + 120
while Path(f'/proc/{pid}/cmdline').exists():
    try:
        if b'qemu' not in Path(f'/proc/{pid}/cmdline').read_bytes():
            break
    except FileNotFoundError:
        break
    if time.monotonic() > deadline:
        raise TimeoutError('Desktop VM failed to power off cleanly')
    time.sleep(1)
print('Desktop VM powered off. Image is ready for packaging.', flush=True)

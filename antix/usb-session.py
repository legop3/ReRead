#!/usr/bin/python3
"""Run USB-only management daemons while the gadget is connected to a host.
Linux netlink wakes this watcher on carrier changes; a slow fallback also covers
controllers that omit link notifications. No requests, device services or camera
activity are needed to decide whether the management session exists.
"""
from pathlib import Path
import select
import signal
import socket
import subprocess
import time


def connected():
    # The legacy Poulsbo UDC can retain a stale "configured" state after
    # disconnect. g_ether's carrier follows its actual function connection.
    try:
        carrier = Path('/sys/class/net/usb0/carrier').read_text().strip() == '1'
        configured = any(path.read_text().strip() == 'configured'
                         for path in Path('/sys/class/udc').glob('*/state'))
        return carrier and configured
    except OSError:
        return False


def main():
    events = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, socket.NETLINK_ROUTE)
    events.bind((0, 1))  # RTMGRP_LINK
    processes = []
    def stop(*_):
        raise SystemExit
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    commands = [
        ['busybox', 'udhcpd', '-f', '/etc/reader/udhcpd.conf'],
        ['/usr/local/sbin/reader-web'],
        ['/usr/local/sbin/reader-files'],
        ['dropbear', '-F', '-E', '-p', '192.168.7.1:22', '-r', '/etc/dropbear/dropbear_ed25519_host_key'],
    ]
    Path('/etc/dropbear').mkdir(exist_ok=True)
    if not Path('/etc/dropbear/dropbear_ed25519_host_key').exists():
        subprocess.run(['dropbearkey', '-t', 'ed25519', '-f', '/etc/dropbear/dropbear_ed25519_host_key'], check=True)
    try:
        while True:
            active = connected()
            if active and not processes:
                print('USB host connected: starting management', flush=True)
                processes = [subprocess.Popen(command) for command in commands]
            if processes and (not active or any(p.poll() is not None for p in processes)):
                for process in processes:
                    if process.poll() is None:
                        process.terminate()
                for process in processes:
                    try:
                        process.wait(5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                processes.clear()
                print('USB management stopped', flush=True)
                if active:
                    time.sleep(2)
            if select.select([events], [], [], 30)[0]:
                events.recv(65536)
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()


if __name__ == '__main__':
    main()

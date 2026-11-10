"""Install the experimental Poulsbo UDC port and USB network services."""
from pathlib import Path
import shutil
import subprocess


def build_module(root, base, build, kernel):
    def run(*args, **kwargs):
        subprocess.run(args, check=True, **kwargs)

    # Rebuild header helper programs for the builder host, not the i386 target.
    headers = build / 'module-headers'
    if headers.exists():
        shutil.rmtree(headers)
    shutil.copytree(root / 'usr/src' / ('linux-headers-' + kernel), headers, symlinks=True)
    run('cc', '-O2', '-o', str(headers / 'scripts/basic/fixdep'), str(headers / 'scripts/basic/fixdep.c'))
    run('cc', '-O2', '-o', str(headers / 'scripts/mod/modpost'),
        *[str(headers / 'scripts/mod' / p) for p in ['modpost.c', 'file2alias.c', 'sumversion.c']])
    module = build / 'iusbc'
    module.mkdir(exist_ok=True)
    for name in ['iusbc.c', 'iusbc.h', 'Makefile']:
        shutil.copy(base / 'antix/iusbc' / name, module / name)
    run('make', '-C', str(headers), 'M=' + str(module), 'ARCH=x86', 'modules')
    return module / 'iusbc.ko'


def configure(root, base, build, kernel):
    """Install distro dependencies; ReRead files are owned by its package."""
    def run(*args):
        subprocess.run(args, check=True)

    shutil.copy('/etc/resolv.conf', root / 'etc/resolv.conf')
    run('chroot', str(root), 'apt-get', 'update', '-qq')
    policy = root / 'usr/sbin/policy-rc.d'
    old_policy = policy.read_bytes() if policy.exists() else None
    old_mode = policy.stat().st_mode & 0o777 if policy.exists() else None
    policy.write_text('#!/bin/sh\nexit 101\n')
    policy.chmod(0o755)
    try:
        run('chroot', str(root), 'apt-get', 'install', '-y', '--no-install-recommends',
            'desktop-file-utils', 'shared-mime-info', 'libglib2.0-bin', 'xdg-utils', 'xserver-xorg-core', 'xserver-xorg-video-fbdev', 'xserver-xorg-input-libinput', 'xinit', 'xinput', 'matchbox-window-manager', 'python3-jinja2', 'python3-jsonschema', 'ca-certificates', 'dropbear-bin', 'chrony', 'alsa-utils', 'python3-usb', 'python3-dbus', 'python3-gi', 'python3-apt', 'upower', 'elogind', 'libpam-elogind', 'triggerhappy', 'libstdc++6', 'libusb-1.0-0', 'libjpeg62-turbo', 'fxload', 'libqt5network5t64', 'libqt5widgets5t64', 'libqt5gui5t64', 'libqt5dbus5t64', 'libqt5svg5', 'qt5-image-formats-plugins', 'libgstreamer1.0-0', 'gstreamer1.0-plugins-base', 'gstreamer1.0-plugins-good', 'gstreamer1.0-alsa', 'hicolor-icon-theme', 'fonts-dejavu-core')
    finally:
        if old_policy is None:
            policy.unlink()
        else:
            policy.write_bytes(old_policy)
            policy.chmod(old_mode)
    run('chroot', str(root), 'apt-get', 'clean')



def stage(root, base):
    """Render package-owned defaults and services from repository sources."""
    def write(path, text, mode=0o644):
        p = root / path.lstrip('/')
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        p.chmod(mode)

    for source, target in [('usb-start.sh', 'usr/local/sbin/reader-usb'),
                           ('usb-session.py', 'usr/local/sbin/reader-usb-session'),
                           ('web.py', 'usr/local/sbin/reader-web'),
                           ('web.html', 'usr/share/reader/web.html')]:
        p = root / target
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(base / 'antix' / source, p)
        p.chmod(0o644 if source.endswith('.html') else 0o755)
    write('/etc/reader/udhcpd.conf', '''start 192.168.7.2
end 192.168.7.2
interface usb0
option subnet 255.255.255.0
option lease 3600
lease_file /var/lib/reader/udhcpd.leases
pidfile /run/reader-udhcpd.pid
''')
    # No router/DNS options: the USB link must not take over the host's Internet route.
    services = root / 'etc/runit/runsvdir/default'
    services.mkdir(parents=True, exist_ok=True)
    runs = {'reader-usb': 'exec /usr/local/sbin/reader-usb\n'}
    for name, body in runs.items():
        write('/etc/sv/' + name + '/run', '#!/bin/sh\nmkdir -p /var/log/reader\n' + body, 0o755)
        link = services / name
        if not link.is_symlink():
            link.symlink_to('/etc/sv/' + name)
    configure_wifi(root, base)


def configure_wifi(root, base):
    """Generic interface discovery; retain the distro DHCP client."""
    from wifi import HEADER
    for source, target, mode in [
        ('wifi-start.sh', 'usr/local/sbin/reader-wifi', 0o755),
        ('wifi.py', 'usr/local/sbin/reader_wifi.py', 0o644),
    ]:
        p = root / target
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(base / 'antix' / source, p)
        p.chmod(mode)
    config = root / 'etc/wpa_supplicant/reader.conf'
    config.parent.mkdir(parents=True, exist_ok=True)
    if not config.exists():
        config.write_text(HEADER)
    config.chmod(0o600)
    service = root / 'etc/sv/reader-wifi/run'
    service.parent.mkdir(parents=True, exist_ok=True)
    service.write_text('#!/bin/sh\nexec /usr/local/sbin/reader-wifi\n')
    service.chmod(0o755)
    link = root / 'etc/runit/runsvdir/default/reader-wifi'
    if not link.is_symlink():
        link.symlink_to('/etc/sv/reader-wifi')
    configure_clock(root)


def configure_clock(root):
    config = root / 'etc/chrony/reader.conf'
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text('''pool 2.debian.pool.ntp.org iburst
driftfile /var/lib/chrony/chrony.drift
makestep 1.0 3
rtcsync
port 0
cmdport 0
''')
    service = root / 'etc/sv/chrony/run'
    service.parent.mkdir(parents=True, exist_ok=True)
    service.write_text('''#!/bin/sh
mkdir -p /var/log/reader
exec /usr/sbin/chronyd -d -f /etc/chrony/reader.conf >>/var/log/reader/time.log 2>&1
''')
    service.chmod(0o755)
    link = root / 'etc/runit/runsvdir/default/chrony'
    if not link.is_symlink():
        link.symlink_to('/etc/sv/chrony')

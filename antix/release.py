"""Build the ReRead platform package from source. Used by fresh installs too."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
from gadget import stage, build_module

KERNEL = '5.10.256-antix.1-486-smp'


def build(base, build_dir, output, kernel=KERNEL, version=None):
    app_version = (base / 'antix/apps/camera/VERSION').read_text().strip()
    version = version or (base / 'antix/VERSION').read_text().strip()
    subprocess.run(['dpkg', '--validate-version', version], check=True)
    root = build_dir / 'reread-package'
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    module = build_module(build_dir / "base-root", base, build_dir, kernel)
    kernel_package_version = subprocess.run(
        ['dpkg-query', '--admindir=' + str(build_dir / 'root/var/lib/dpkg'), '-W', '-f=${Version}', 'linux-image-' + kernel],
        text=True, capture_output=True, check=True).stdout.strip()
    stage(root, base)

    def copy(source, target, mode=0o644):
        path = root / target
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(source, path)
        path.chmod(mode)

    def write(target, text, mode=0o644):
        path = root / target
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        path.chmod(mode)

    subprocess.run(['gcc', '-m32', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-o', str(build_dir / 'buttons'), str(base / 'antix/buttons.c')], check=True)
    copy(build_dir / 'buttons', 'usr/local/bin/reader-buttons', 0o755)
    subprocess.run(['gcc', '-m32', '-Os', '-Wall', '-Wextra', '-Werror',
                    '-o', str(build_dir / 'fan-reset'), str(base / 'antix/fan-reset.c')], check=True)
    copy(build_dir / 'fan-reset', 'usr/local/sbin/reader-fan-reset', 0o755)
    for source, name in [('power.sh', 'reader-power'), ('start-reader.sh', 'start-reader'),
                         ('battery.py', 'reader_battery.py'),
                         ('audio.py', 'reader-audio'),
                         ('brightness.py', 'reader-brightness'),
                         ('camera.py', 'reader_camera.py'),
                         ('camera_backend.py', 'reader_camera_backend.py'),
                         ('manager.py', 'reader_manager.py'), ('update.py', 'reader-update')]:
        copy(base / 'antix' / source, 'usr/local/sbin/' + name, 0o755)
    ui = build_dir / 'ui'
    subprocess.run([str(base / 'antix/ui/build.sh'), str(ui)], check=True)
    copy(ui / 'libreader-ui.so.2', 'usr/lib/libreader-ui.so.2', 0o755)
    (root / 'usr/lib/libreader-ui.so').symlink_to('libreader-ui.so.2')
    copy(ui / 'reader-launcher', 'usr/local/bin/reader-launcher', 0o755)
    copy(base / 'antix/ui/reader-ui.hpp', 'usr/include/reread/reader-ui.hpp')
    copy(base / 'antix/reader-keys.h', 'usr/include/reread/reader-keys.h')
    write('usr/lib/i386-linux-gnu/pkgconfig/reread-ui.pc', f"""prefix=/usr
Name: ReRead UI
Description: ReRead framebuffer UI and physical controls
Version: {version}
Requires: Qt5Widgets Qt5Network
Libs: -L/usr/lib -lreader-ui
Cflags: -I/usr/include/reread
""")
    write('etc/reader/display.json', '{"brightness_percent":100,"idle_dim_seconds":120}\n')
    native_camera = build_dir / 'libreader-camera.so'
    subprocess.run(['g++', '-m32', '-std=c++17', '-O3', '-march=bonnell',
                    '-Wall', '-Wextra', '-Werror', '-fPIC', '-shared', '-pthread',
                    *[str(base / 'antix' / source) for source in
                      ('camera_native.cpp', 'camera_hardware.cpp', 'camera_processing.cpp')],
                    '-lusb-1.0', '-ljpeg', '-o', str(native_camera)], check=True)
    copy(native_camera, 'usr/lib/reread/libreader-camera.so', 0o644)
    # Owner-supplied controller firmware is a tiny build input, not a dependency
    # on the stock operating system. Packages contain it for offline installs.
    firmware = base / 'build/firmware/CameraCtlr.hex'
    if not firmware.is_file():
        raise RuntimeError('Provide build/firmware/CameraCtlr.hex from your Reader backup; see antix/README.md.')
    if hashlib.sha256(firmware.read_bytes()).hexdigest() != '61d003fca740a8ea4901c73f6a8fc2a07fbb3d1d8b203231e51fda1138a9b23e':
        raise RuntimeError('CameraCtlr.hex does not match the investigated Reader firmware.')
    copy(firmware, 'lib/firmware/reread/CameraCtlr.hex')
    write('usr/local/sbin/reader-camera', '#!/bin/sh\nexec /usr/bin/python3 /usr/local/sbin/reader_camera.py "$@"\n', 0o755)
    write('etc/sv/reader-camera/run', '#!/bin/sh\nmkdir -p /var/log/reader\nexec /usr/local/sbin/reader-camera serve >>/var/log/reader/camera.log 2>&1\n', 0o755)
    (root / 'etc/runit/runsvdir/default/reader-camera').symlink_to('/etc/sv/reader-camera')
    for font in ['font-large.psf', 'font-tall.psf']:
        copy(base / 'antix/assets' / font, 'usr/share/reader/' + font)
    copy(base / 'antix/assets/COPYING', 'usr/share/doc/reread/font-COPYING')
    copy(base / 'antix/iusbc/COPYING', 'usr/share/doc/reread/iusbc-COPYING')
    copy(module, 'lib/modules/' + kernel + '/extra/iusbc.ko')
    backlight = build_dir / 'backlight'
    backlight.mkdir(exist_ok=True)
    for name in ('reader_backlight.c', 'Makefile'):
        shutil.copy(base / 'antix/backlight' / name, backlight / name)
    subprocess.run(['make', '-C', str(build_dir / 'module-headers'),
                    'M=' + str(backlight), 'ARCH=x86', 'modules'], check=True)
    copy(backlight / 'reader_backlight.ko', 'lib/modules/' + kernel + '/extra/reader_backlight.ko')
    write('usr/share/reader/version', version + '\n')
    write('usr/share/reader/kernel-version', kernel + '\n')
    write('etc/reader/power.json', '{"warning_percent":12,"critical_percent":6,"shutdown_delay_seconds":30}\n')
    write('etc/sv/reader-battery/run', '#!/bin/sh\nmkdir -p /var/log/reader\nexec /usr/local/sbin/reader_battery.py >>/var/log/reader/battery.log 2>&1\n', 0o755)
    (root / 'etc/runit/runsvdir/default/reader-battery').symlink_to('/etc/sv/reader-battery')
    write('etc/sv/reader/run', '#!/bin/sh\nexec /usr/local/sbin/start-reader\n', 0o755)
    (root / 'etc/runit/runsvdir/default/reader').symlink_to('/etc/sv/reader')
    write('DEBIAN/control', f'''Package: reread
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: admin
Priority: optional
Depends: libqt5network5t64, libqt5widgets5t64, libqt5gui5t64, fonts-dejavu-core, libc6 (>= 2.36), python3, python3-usb, libstdc++6, libusb-1.0-0, libjpeg62-turbo, fxload, runit-antix, dropbear-bin, chrony, wpasupplicant, dhcpcd-base, kmod, console-setup-linux, alsa-utils, linux-image-{kernel} (= {kernel_package_version})
Breaks: reread-app-camera (<< 0.2.0)
Description: ReRead handheld platform for Intel Reader
 Button interface, power management, USB networking, web management,
 Wi-Fi setup, platform updates and application management.
''')
    write('DEBIAN/conffiles', '/etc/wpa_supplicant/reader.conf\n/etc/reader/udhcpd.conf\n/etc/chrony/reader.conf\n/etc/reader/power.json\n/etc/reader/display.json\n')
    copy(base / 'antix/postinst.sh', 'DEBIAN/postinst', 0o755)
    output.mkdir(parents=True, exist_ok=True)
    package = output / ('reread_' + version + '_i386.deb')
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(root), str(package)], check=True)
    app = build_dir / 'reread-app-camera'
    if app.exists(): shutil.rmtree(app)
    def appwrite(path, content):
        dest = app / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(content)
    appwrite('DEBIAN/control', f"""Package: reread-app-camera
Version: {app_version}
Architecture: i386
Maintainer: ReRead contributors
Section: graphics
Priority: optional
Depends: reread (>= 0.6.0)
Description: ReRead camera
 Native button-operated camera with live preview and full-resolution photos.
""")
    appwrite('usr/share/applications/reread-camera.desktop', """[Desktop Entry]
Type=Application
Name=Camera
Comment=Live view, photographs and documents
Exec=/usr/lib/reread/apps/camera/reader-camera-app
Icon=camera-photo
Categories=Graphics;Photography;
X-ReRead=true
Terminal=false
""")
    appwrite('usr/share/reread/apps/reread-app-camera.json', '{"name":"Camera","description":"Live view, photographs and documents","desktop":"reread-camera.desktop"}\n')
    target = app / 'usr/lib/reread/apps/camera/reader-camera-app'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(ui / 'reader-camera', target)
    target.chmod(0o755)
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(app), str(output / f'reread-app-camera_{app_version}_i386.deb')], check=True)
    return package


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', help='Release version; defaults to antix/VERSION')
    args = parser.parse_args()
    base = Path(__file__).resolve().parent.parent
    print(build(base, base / 'build/antix', base / 'out/antix', version=args.version))

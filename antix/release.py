"""Build the ReRead platform package from source. Used by fresh installs too."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
from gadget import stage, build_module
from third_party import stage_file_manager, stage_settings_editor
from apps.images.package import build as build_images
from apps.text.package import build as build_text
from apps.audio.package import build as build_audio

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
    stage_file_manager(root, base)
    stage_settings_editor(root, base)

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
                         ('reader-service.sh', 'reader-service'),
                         ('volume-key.sh', 'reader-volume-key'),
                         ('battery.py', 'reader_battery.py'),
                         ('upower-configure.py', 'reader-upower-configure'),
                         ('audio.py', 'reader-audio'),
                         ('brightness.py', 'reader-brightness'),
                         ('camera.py', 'reader_camera.py'),
                         ('camera_backend.py', 'reader_camera_backend.py'),
                         ('app_settings.py', 'reader_app_settings.py'),
                         ('manager.py', 'reader_manager.py'), ('repositories.py', 'reader_repositories.py'),
                         ('files.sh', 'reader-files'), ('software.py', 'reader-software'), ('update.py', 'reader-update')]:
        copy(base / 'antix' / source, 'usr/local/sbin/' + name, 0o755)
    subprocess.run(['python3',str(base/'antix/input/generate.py'),'--check'],check=True)
    ui = build_dir / 'ui'
    subprocess.run([str(base / 'antix/ui/build.sh'), str(ui)], check=True)
    subprocess.run([str(base / 'antix/apps/camera/build.sh'), str(ui)], check=True)
    subprocess.run([str(base / 'antix/apps/software/build.sh'), str(ui)], check=True)
    subprocess.run([str(base / 'antix/apps/files/build.sh'), str(ui)], check=True)
    copy(ui / 'libreader-ui.so.5', 'usr/lib/libreader-ui.so.5', 0o755)
    (root / 'usr/lib/libreader-ui.so').symlink_to('libreader-ui.so.5')
    copy(ui / 'libreader-device.so.1', 'usr/lib/libreader-device.so.1', 0o755)
    (root / 'usr/lib/libreader-device.so').symlink_to('libreader-device.so.1')
    copy(base / 'antix/ui/reader-device.hpp', 'usr/include/reread/reader-device.hpp')
    write('usr/lib/i386-linux-gnu/pkgconfig/reread-device.pc', f"""prefix=/usr
Name: ReRead device helpers
Description: Optional QtCore/QtDBus Reader navigation and session integration
Version: {version}
Requires: Qt5Core Qt5DBus
Libs: -L/usr/lib -lreader-device
Cflags: -I/usr/include/reread
""")
    copy(ui / 'reader-launcher', 'usr/local/bin/reader-launcher', 0o755)
    copy(ui / 'reader-session', 'usr/local/libexec/reader-session', 0o755)
    copy(base / 'antix/x11/xorg.conf', 'usr/share/reader/xorg.conf')
    copy(base / 'antix/x11/session.sh', 'usr/local/libexec/reader-x11-session', 0o755)
    for asset in (base / 'antix/input/icons').glob('*.svg'):
        copy(asset, 'usr/share/icons/hicolor/scalable/actions/' + asset.name)
    copy(base / 'antix/input/buttons.json', 'usr/share/reread/buttons.json')
    copy(base / 'antix/input/README.md', 'usr/share/doc/reread/buttons.md')
    copy(base / 'antix/input/button-reference.svg', 'usr/share/doc/reread/button-reference.svg')
    copy(base / 'antix/input/70-reread-keyboard.hwdb', 'usr/lib/udev/hwdb.d/70-reread-keyboard.hwdb')
    copy(base / 'antix/input/reader-volume.conf', 'etc/triggerhappy/triggers.d/reader-volume.conf')
    copy(base / 'antix/power-hardware.sh', 'etc/elogind/system-sleep/50-reread', 0o755)
    write('etc/elogind/logind.conf.d/60-reread.conf', '[Login]\nHandlePowerKey=suspend\nHandlePowerKeyLongPress=ignore\nHandleLidSwitch=ignore\nKillUserProcesses=no\nIdleAction=ignore\n')
    write('etc/elogind/sleep.conf.d/60-reread.conf', '[Sleep]\nAllowSuspend=yes\nAllowHibernation=no\nAllowSuspendThenHibernate=no\nAllowHybridSleep=no\nSuspendState=mem\nSuspendMode=deep\nAllowSuspendInterrupts=yes\n')
    write('etc/sv/elogind/run', '#!/bin/sh\nmkdir -p /var/log/reader\nexec /usr/libexec/elogind >>/var/log/reader/elogind.log 2>&1\n', 0o755)
    write('etc/sv/reader-hotkeys/run', '#!/bin/sh\nmkdir -p /var/log/reader\nexec /usr/sbin/thd --triggers /etc/triggerhappy/triggers.d/ --socket /run/thd.socket --deviceglob "/dev/input/event*" >>/var/log/reader/hotkeys.log 2>&1\n', 0o755)
    copy(base / 'antix/ui/reader-ui.hpp', 'usr/include/reread/reader-ui.hpp')
    copy(base / 'antix/reader-keys.h', 'usr/include/reread/reader-keys.h')
    write('usr/lib/i386-linux-gnu/pkgconfig/reread-ui.pc', f"""prefix=/usr
Name: ReRead UI
Description: Optional ReRead Qt drawing helpers
Version: {version}
Requires: reread-device Qt5Widgets
Libs: -L/usr/lib -lreader-ui
Cflags: -I/usr/include/reread
""")
    copy(base / 'repository/reread-archive-keyring.asc', 'usr/share/keyrings/reread-archive-keyring.asc')
    copy(base / 'repository/reread.sources', 'etc/apt/sources.list.d/reread.sources')
    write('etc/reader/display.json', '{"brightness_percent":100,"idle_dim_seconds":120,"auto_suspend_seconds":0}\n')
    for asset in (base / 'antix/ui/icons').iterdir():
        target = 'index.theme' if asset.name == 'index.theme' else 'scalable/apps/' + asset.name
        copy(asset, 'usr/share/icons/reread/' + target)
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
    write('etc/sv/reader-battery/run', '#!/bin/sh\nmkdir -p /var/log/reader\nexec /usr/local/sbin/reader_battery.py >>/var/log/reader/battery.log 2>&1\n', 0o755)
    (root / 'etc/runit/runsvdir/default/reader-battery').symlink_to('/etc/sv/reader-battery')
    write('etc/sv/reader/run', '#!/bin/sh\nexec /usr/local/sbin/reader-service\n', 0o755)
    (root / 'etc/runit/runsvdir/default/reader').symlink_to('/etc/sv/reader')
    write('DEBIAN/control', f'''Package: reread
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: admin
Priority: optional
Depends: desktop-file-utils, shared-mime-info, libglib2.0-bin, xdg-utils, xserver-xorg-core, xserver-xorg-video-fbdev, xserver-xorg-input-libinput, xinit, xinput, matchbox-window-manager, ca-certificates, libqt5widgets5t64, libqt5gui5t64, libqt5dbus5t64, libqt5svg5, hicolor-icon-theme, fonts-dejavu-core, libc6 (>= 2.36), python3, python3-jinja2, python3-jsonschema, python3-dbus, python3-gi, python3-apt, upower, libglib2.0-0t64, python3-usb, libstdc++6, libusb-1.0-0, libjpeg62-turbo, fxload, runit-antix, elogind, libpam-elogind, triggerhappy, dropbear-bin, chrony, wpasupplicant, dhcpcd-base, kmod, console-setup-linux, alsa-utils, linux-image-{kernel} (= {kernel_package_version})
Recommends: reread-app-files, reread-app-images, reread-app-text, reread-app-audio
Breaks: reread-app-camera (<< 0.8.0), reread-app-files (<< 0.6.0), reread-app-software (<< 0.4.0), reread-app-images (<< 0.2.0), reread-app-text (<< 0.2.0), reread-app-audio (<< 0.2.0), reread-app-archive (<< 0.9.0), reread-app-booru (<< 0.4.0)
Description: ReRead handheld platform for Intel Reader
 Button interface, power management, USB networking, web management,
 Wi-Fi setup, platform updates and application management.
''')
    write('DEBIAN/conffiles', '/etc/xdg/reread-mimeapps.list\n/etc/apt/sources.list.d/reread.sources\n/etc/wpa_supplicant/reader.conf\n/etc/reader/udhcpd.conf\n/etc/chrony/reader.conf\n/etc/reader/display.json\n/etc/reader/files.conf\n/etc/dpkg/dpkg.cfg.d/80-reread-space\n/etc/apt/apt.conf.d/80-reread-indexes\n')
    copy(base / 'antix/x11/reread-mimeapps.list', 'etc/xdg/reread-mimeapps.list')
    copy(base / 'antix/files-head.html', 'usr/share/reader/files-head.html')
    copy(base / 'antix/storage/80-reread-space', 'etc/dpkg/dpkg.cfg.d/80-reread-space')
    copy(base / 'antix/storage/80-reread-indexes', 'etc/apt/apt.conf.d/80-reread-indexes')
    copy(base / 'antix/app-settings.js', 'usr/share/reader/app-settings.js')
    copy(base / 'antix/files.conf', 'etc/reader/files.conf')
    copy(base / 'antix/STORAGE.md', 'usr/share/doc/reread/storage.md')
    copy(base / 'antix/THIRD_PARTY.md', 'usr/share/doc/reread/third-party.md')
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
Depends: reread (>= 0.21.0), libqt5network5t64
Recommends: reread-app-images
Description: ReRead camera
 Native button-operated camera with live preview and full-resolution photos.
""")
    appwrite('usr/share/applications/reread-camera.desktop', """[Desktop Entry]
Type=Application
Name=Camera
Comment=Live view, photographs and documents
Exec=/usr/lib/reread/apps/camera/reader-camera-app
Icon=reread-camera
Categories=Graphics;Photography;
X-ReRead=true
Terminal=false
""")
    appwrite('usr/share/reread/apps/reread-app-camera.json', '{"name":"Camera","description":"Live view, photographs and documents","desktop":"reread-camera.desktop"}\n')
    camera_icon = app / 'usr/share/icons/hicolor/scalable/apps/reread-camera.svg'
    camera_icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(base / 'antix/apps/camera/reread-camera.svg', camera_icon)
    target = app / 'usr/lib/reread/apps/camera/reader-camera-app'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(ui / 'reader-camera-app', target)
    target.chmod(0o755)
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(app), str(output / f'reread-app-camera_{app_version}_i386.deb')], check=True)
    app = build_dir / 'reread-app-software'
    if app.exists(): shutil.rmtree(app)
    software_version = (base / 'antix/apps/software/VERSION').read_text().strip()
    appwrite('DEBIAN/control', f"""Package: reread-app-software
Version: {software_version}
Architecture: i386
Maintainer: ReRead contributors
Section: admin
Priority: optional
Depends: reread (>= 0.20.0)
Description: ReRead Software manager
 Check and install system updates, browse applications, and install or remove apps.
""")
    appwrite('usr/share/applications/reread-software.desktop', """[Desktop Entry]
Type=Application
Name=Software
Comment=System updates and applications
Exec=/usr/lib/reread/apps/software/reader-software-app
Icon=reread-software
Categories=System;PackageManager;
X-ReRead=true
Terminal=false
""")
    appwrite('usr/share/reread/apps/reread-app-software.json', '{"name":"Software","description":"System updates and applications","desktop":"reread-software.desktop"}\n')
    software_icon = app / 'usr/share/icons/hicolor/scalable/apps/reread-software.svg'
    software_icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(base / 'antix/apps/software/reread-software.svg', software_icon)
    target = app / 'usr/lib/reread/apps/software/reader-software-app'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(ui / 'reader-software-app', target)
    target.chmod(0o755)
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(app), str(output / f'reread-app-software_{software_version}_i386.deb')], check=True)
    app = build_dir / 'reread-app-files'
    if app.exists(): shutil.rmtree(app)
    files_version = (base / 'antix/apps/files/VERSION').read_text().strip()
    appwrite('DEBIAN/control', f"""Package: reread-app-files
Version: {files_version}
Architecture: i386
Maintainer: ReRead contributors
Section: utils
Priority: optional
Depends: reread (>= 0.20.0)
Recommends: reread-app-images, reread-app-text, reread-app-audio
Description: ReRead Files
 Browse shared folders, open files in default viewers, inspect and delete files.
""")
    appwrite('usr/share/applications/reread-files.desktop', """[Desktop Entry]
Type=Application
Name=Files
Comment=Browse folders, view and manage files
Exec=/usr/lib/reread/apps/files/reader-files-app
Icon=reread-files
Categories=Utility;FileManager;
X-ReRead=true
Terminal=false
""")
    appwrite('usr/share/reread/apps/reread-app-files.json', '{"name":"Files","description":"Browse folders, view and manage files","desktop":"reread-files.desktop"}\n')
    target = app / 'usr/share/icons/hicolor/scalable/apps/reread-files.svg'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(base / 'antix/apps/files/reread-files.svg', target)
    target = app / 'usr/lib/reread/apps/files/reader-files-app'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(ui / 'reader-files-app', target)
    target.chmod(0o755)
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(app), str(output / f'reread-app-files_{files_version}_i386.deb')], check=True)
    build_images(base, build_dir, output)
    build_text(base, build_dir, output)
    build_audio(base, build_dir, output)
    return package


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', help='Release version; defaults to antix/VERSION')
    args = parser.parse_args()
    base = Path(__file__).resolve().parent.parent
    print(build(base, base / 'build/antix', base / 'out/antix', version=args.version))

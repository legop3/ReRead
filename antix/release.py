"""Build the ReRead platform package from source. Used by fresh installs too."""
import argparse
from pathlib import Path
import shutil
import subprocess
from gadget import stage, build_module

KERNEL = '5.10.256-antix.1-486-smp'


def build(base, build_dir, output, kernel=KERNEL, version=None):
    version = version or (base / 'antix/VERSION').read_text().strip()
    subprocess.run(['dpkg', '--validate-version', version], check=True)
    root = build_dir / 'reread-package'
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    module = build_module(build_dir / "root", base, build_dir, kernel)
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

    subprocess.run(['gcc', '-m32', '-Os', '-nostdlib', '-static', '-fno-pie', '-no-pie',
                    '-fno-stack-protector', '-fno-builtin', '-Wall', '-Wextra', '-Werror',
                    '-o', str(build_dir / 'buttons'), str(base / 'antix/buttons.c')], check=True)
    copy(build_dir / 'buttons', 'usr/local/bin/reader-buttons', 0o755)
    for source, name in [('power.sh', 'reader-power'), ('start-reader.sh', 'start-reader'),
                         ('manager.py', 'reader_manager.py'), ('update.py', 'reader-update')]:
        copy(base / 'antix' / source, 'usr/local/sbin/' + name, 0o755)
    for font in ['font-large.psf', 'font-tall.psf']:
        copy(base / 'antix/assets' / font, 'usr/share/reader/' + font)
    copy(base / 'antix/assets/COPYING', 'usr/share/doc/reread/font-COPYING')
    copy(base / 'antix/iusbc/COPYING', 'usr/share/doc/reread/iusbc-COPYING')
    copy(module, 'lib/modules/' + kernel + '/extra/iusbc.ko')
    write('usr/share/reader/version', version + '\n')
    write('usr/share/reader/kernel-version', kernel + '\n')
    write('etc/sv/reader/run', '#!/bin/sh\nexec /usr/local/sbin/start-reader\n', 0o755)
    (root / 'etc/runit/runsvdir/default/reader').symlink_to('/etc/sv/reader')
    write('DEBIAN/control', f'''Package: reread
Version: {version}
Architecture: i386
Maintainer: ReRead contributors
Section: admin
Priority: optional
Depends: python3, runit-antix, dropbear-bin, chrony, wpasupplicant, dhcpcd-base, kmod, console-setup-linux, linux-image-{kernel} (= {kernel_package_version})
Description: ReRead handheld platform for Intel Reader
 Button interface, power management, USB networking, web management,
 Wi-Fi setup, platform updates and application management.
''')
    write('DEBIAN/conffiles', '/etc/wpa_supplicant/reader.conf\n/etc/reader/udhcpd.conf\n/etc/chrony/reader.conf\n')
    copy(base / 'antix/postinst.sh', 'DEBIAN/postinst', 0o755)
    output.mkdir(parents=True, exist_ok=True)
    package = output / ('reread_' + version + '_i386.deb')
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(root), str(package)], check=True)
    return package


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', help='Release version; defaults to antix/VERSION')
    args = parser.parse_args()
    base = Path(__file__).resolve().parent.parent
    print(build(base, base / 'build/antix', base / 'out/antix', version=args.version))

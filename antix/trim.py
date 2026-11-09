"""Keep a normal apt-managed runtime without the live ISO's build/install baggage."""
from pathlib import Path
import shutil
import subprocess


# None of these packages supply USB Wi-Fi support. iwlwifi and SOF target
# PCI/DSP hardware absent from the Reader; its installed bootloader is Syslinux.
COMPACT_PACKAGES = {
    'firmware-iwlwifi', 'firmware-sof-signed', 'grub-common', 'grub2-common',
    'grub-pc', 'grub-pc-bin', 'grub-efi-amd64-bin', 'grub-efi-amd64-unsigned',
    'grub-efi-ia32-bin', 'grub-efi-ia32-unsigned',
}


def policy(root):
    for source, target in [('80-reread-space', 'etc/dpkg/dpkg.cfg.d/80-reread-space'),
                           ('80-reread-indexes', 'etc/apt/apt.conf.d/80-reread-indexes')]:
        dest = root / target
        if not dest.exists():
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(Path(__file__).with_name('storage') / source, dest)


def compact(root):
    policy(root)
    result = subprocess.run(['chroot', str(root), 'dpkg-query', '-W',
                             '-f=${Package} ${db:Status-Status}\n'],
                            text=True, capture_output=True, check=True)
    installed = {line.split()[0] for line in result.stdout.splitlines()
                 if line.endswith(' installed')}
    packages = sorted(COMPACT_PACKAGES & installed)
    if packages:
        subprocess.run(['chroot', str(root), 'apt-get', '-y', 'purge', *packages],
                       check=True, env={'PATH': '/usr/sbin:/usr/bin:/sbin:/bin',
                                        'DEBIAN_FRONTEND': 'noninteractive'})
    # dpkg exclusions affect future unpacking; remove existing excluded catalogs.
    locales = root / 'usr/share/locale'
    if locales.is_dir():
        for path in locales.iterdir():
            if path.name.startswith('en') or path.name == 'locale.alias':
                continue
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path)
            else:
                path.unlink()
    for name in ['pkgcache.bin', 'srcpkgcache.bin']:
        (root / 'var/cache/apt' / name).unlink(missing_ok=True)
    # Drop only translated descriptions, keeping all package/dependency indexes.
    for path in (root / 'var/lib/apt/lists').glob('*Translation-*'):
        if path.is_file():
            path.unlink()


def runtime(root):
    def chroot(*args):
        return subprocess.run(['chroot', str(root), *args], check=True,
                              env={'PATH': '/usr/sbin:/usr/bin:/sbin:/bin',
                                   'DEBIAN_FRONTEND': 'noninteractive'})

    # Use one generated locale instead of shipping every precompiled locale.
    # Other locales can still be enabled with the distro's locale-gen command.
    (root/'etc/locale.gen').write_text('en_US.UTF-8 UTF-8\n')
    (root/'etc/default/locale').write_text('LANG=en_US.UTF-8\n')
    # Keep broad firmware support even after dropping the AMD graphics meta-package.
    result = subprocess.run(['chroot', str(root), 'dpkg-query', '-W',
                             '-f=${Package} ${db:Status-Status}\n'],
                            text=True, capture_output=True, check=True)
    installed = {line.split()[0] for line in result.stdout.splitlines()
                 if line.endswith(' installed')}
    firmware = sorted(name for name in installed if name.startswith('firmware-')
                      and name not in ({'firmware-linux', 'firmware-linux-nonfree', 'firmware-amd-graphics'} | COMPACT_PACKAGES))
    chroot('apt-mark', 'manual', *firmware)
    unwanted = {'locales-all', 'firmware-linux', 'firmware-linux-nonfree',
                'firmware-amd-graphics', 'memtest86+', 'iso-template-antix',
                'live-kernel-updater', 'cli-installer-antix', 'os-prober'}
    unwanted.update(name for name in installed
                    if name.startswith('linux-headers-'))
    # No autoremove: keep the distro's remaining runtime and adapter support.
    chroot('apt-get', '-y', '--no-upgrade', 'purge', *sorted(unwanted & installed))
    chroot('locale-gen')
    chroot('apt-get', 'clean')
    compact(root)
    # Retain apt's package indexes so apps can resolve new dependencies.
    for directory in ['var/cache/apt/archives']:
        path = root/directory
        shutil.rmtree(path)
        (path/'partial').mkdir(parents=True)
    # The live ISO's generated package caches aren't needed on an installed OS.
    for name in ['pkgcache.bin', 'srcpkgcache.bin']:
        (root/'var/cache/apt'/name).unlink(missing_ok=True)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Remove unused ReRead runtime packages and UI translations.')
    parser.parse_args()
    if not Path('/boot/syslinux.cfg').is_file() or not Path('/usr/share/reader/version').is_file():
        raise SystemExit('Run this cleanup only on an installed ReRead system using Syslinux.')
    compact(Path('/'))

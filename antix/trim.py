"""Keep a normal apt-managed runtime without the live ISO's build/install baggage."""
import shutil
import subprocess


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
                      and name not in {'firmware-linux', 'firmware-linux-nonfree', 'firmware-amd-graphics'})
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
    # Retain apt's package indexes so apps can resolve new dependencies.
    for directory in ['var/cache/apt/archives']:
        path = root/directory
        shutil.rmtree(path)
        (path/'partial').mkdir(parents=True)
    # The live ISO's generated package caches aren't needed on an installed OS.
    for name in ['pkgcache.bin', 'srcpkgcache.bin']:
        (root/'var/cache/apt'/name).unlink(missing_ok=True)

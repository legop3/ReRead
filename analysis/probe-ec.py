"""Build a temporary, read-only Colbert EC probe using the release's headers.

Run in the builder after a ReRead release build. This only produces a module;
it neither connects to a Reader nor installs/loads anything automatically.
The stock register-map derivation is recorded in cooling.md.
"""
from pathlib import Path
import subprocess

BASE = Path(__file__).resolve().parents[1]
BUILD = BASE / 'build/antix'
PROBE = BUILD / 'ec-probe'
SOURCE = r'''
// SPDX-License-Identifier: GPL-2.0-only
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

static const u8 registers[] = {
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
};

static int snapshot_get(char *buffer, const struct kernel_param *parameter)
{
    int i, error, length = 0;
    u8 value;
    for (i = 0; i < ARRAY_SIZE(registers); i++) {
        error = ec_read(registers[i], &value);
        if (error)
            return error;
        length += scnprintf(buffer + length, PAGE_SIZE - length,
                            "%02x=%u\n", registers[i], value);
    }
    return length;
}

static const struct kernel_param_ops snapshot_ops = { .get = snapshot_get };
module_param_cb(snapshot, &snapshot_ops, NULL, 0444);
MODULE_PARM_DESC(snapshot, "Read-only temperature and fan register snapshot");

static int __init probe_init(void)
{
    if (!dmi_match(DMI_SYS_VENDOR, "Intel Corporation") ||
        !dmi_match(DMI_PRODUCT_NAME, "Colbert"))
        return -ENODEV;
    return 0;
}
static void __exit probe_exit(void) {}
module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Temporary read-only Intel Reader EC investigation probe");
'''


if __name__ == '__main__':
    headers = BUILD / 'module-headers'
    if not headers.is_dir():
        raise SystemExit('Build a ReRead release first to prepare matching module headers.')
    PROBE.mkdir(parents=True, exist_ok=True)
    (PROBE / 'reader_ec_probe.c').write_text(SOURCE)
    (PROBE / 'Makefile').write_text('obj-m += reader_ec_probe.o\n')
    subprocess.run(['make', '-C', str(headers), 'M=' + str(PROBE), 'ARCH=x86', 'modules'], check=True)
    print(PROBE / 'reader_ec_probe.ko')

# ReRead LCD brightness

ReRead uses Linux's existing backlight class. No additional daemon, direct EC
writer, display driver, or raw register writer is needed. The Python helper is
short-lived control/settings glue; it does not render or refresh the display.

## Interface

`reader-brightness status` (or `get`) returns JSON. `set 10` through `set 100`,
`up`, and `down` adjust brightness and remember it in
`/etc/reader/display.json` as `brightness_percent`. Up/down use ten percentage
points. `init` restores the saved level; the first invocation keeps the current
hardware level. Changes survive platform updates. There is no polling process.

The ten-percent floor keeps brightness controls separate from screen blanking
and sleep. The helper prefers `psb-bl`, then other standard raw/platform/firmware
backlight providers. Maximum values come from sysfs rather than being assumed.
It reports `actual_percent` from the driver's `actual_brightness`; this is a
reported setting, not an optical measurement.

Packaging supplies `/usr/local/sbin/reader-brightness` and initializes it at boot.
The implementation is `antix/brightness.py`; it has no third-party dependencies.

## Reader validation, 2026-11-08

The running Reader exposed both `/sys/class/backlight/psb-bl` (platform, maximum
100) and `acpi_video0` (firmware, maximum 10). Both controlled the same panel
brightness. The original setting was 100 percent and was restored after testing.

To distinguish real hardware changes from cached readback, a diagnostic mapped
PCI display BAR0 read-only and sampled `BLC_PWM_CTL` at offset `0x61254`:

| Setting | Driver readback | PWM register |
| --- | --- | --- |
| Original | psb 100/100, ACPI 10/10 | `0x7a120000` |
| psb set to 40 | psb 40/100 | `0x7a12493e` |
| psb restored | psb 100/100 | `0x7a120000` |
| ACPI set to 4 | psb 40/100, ACPI 4/10 | `0x7a12493e` |
| ACPI restored | psb 100/100, ACPI 10/10 | `0x7a120000` |

This demonstrates changes to the panel's PWM setting, independently of sysfs
readback. No raw register was written. User perception of minimum brightness
has not been checked. No suspend/resume or reboot was needed for these tests.

The helper itself was then run over SSH with temporary settings: set 40, up to
50, down to 40, init restoring saved 40, rejection of 0 and 101, and restoration
to original 100. It selected `psb-bl` throughout. Persistent user configuration
was not changed by this test.

The upstream [Poulsbo backlight driver](https://github.com/torvalds/linux/blob/v5.10/drivers/gpu/drm/gma500/psb_device.c)
registers `psb-bl` as a 100-step platform provider; its getter can return a cached
value. Its setter delegates to the
[LVDS brightness implementation](https://github.com/torvalds/linux/blob/v5.10/drivers/gpu/drm/gma500/psb_intel_lvds.c),
which programs panel PWM or I2C according to the display firmware configuration.
The [register definitions](https://github.com/torvalds/linux/blob/v5.10/drivers/gpu/drm/gma500/psb_intel_reg.h)
identify `BLC_PWM_CTL` at `0x61254`.

## Stock implementation

The stock `libcolbert-hm.so` (SHA256
`d04f176582b5ec6f148a06798778dfa9404161c102be5d59b509fdab2dde8200`)
exports `HMSetLCDBrightness` at `0x97d0`. GNU disassembly shows it checks the
requested percentage against 100 and calls `HMInternalSetEcRamLocation` with
address `0x4f` at `0x9887`. `HMGetLCDBrightness` at `0xbba0` checks LCD power,
then reads EC address `0x40` at `0xbc26`. These are static observations from the
owner's stock image. ReRead does not need to reproduce those EC transactions:
the running kernel already exposes working brightness control.

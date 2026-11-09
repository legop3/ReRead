# ReRead LCD brightness

ReRead uses Linux's backlight class, with a small Intel Reader panel driver
that implements the stock EC brightness command. There is no brightness daemon.
The Python helper is short-lived settings glue; it does not render or refresh
the display. See the physical-validation correction below: the generic gma500
PWM control changed registers but did not visibly dim this panel.

## Interface

`reader-brightness status` (or `get`) returns JSON. `set 10` through `set 100`,
`up`, and `down` adjust brightness and remember it in
`/etc/reader/display.json` as `brightness_percent`. Up/down use ten percentage
points. `init` restores the saved level; the first invocation keeps the current
hardware level. Changes survive platform updates. There is no polling process.

The ten-percent floor keeps brightness controls separate from screen blanking
and sleep. The helper prefers `reader-panel`, then `psb-bl` and standard raw/platform/firmware
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
library register ID `0x4f` at `0x9887`. `HMGetLCDBrightness` at `0xbba0` checks LCD
power, then reads library ID `0x40` at `0xbc26`. These IDs map to physical EC
bytes 0x86 and 0x81 respectively. The initial investigation stopped before
following that mapping; the panel driver now reproduces the traced transaction.

## Correction after physical validation, 2026-11-08

The owner reported **no visible dimming** at Linux `psb-bl` 20%. The PWM
register changes above did not prove physical panel brightness changed. They
must not be treated as successful optical validation.

Following stock `HMSetLCDBrightness` further revealed an important distinction:
its `0x4f` argument is a **library register ID**, not an EC address. The `ec`
table at VA `0x131e0` maps it to physical **EC 0x86**, all eight bits, named
"Set LCD Backlight". Getter ID `0x40` maps to EC **0x81**, bits 1–7.
`HMInternalSetByte` and `HMInternalGetByte` access these bytes through command
`0xf4` at I/O 0x1201, index 0x1202, data 0x1203 (the same stock transport as
the existing fan-reset helper).

REA evidence `ev_471287bcd5d275a217b696acd636618b82079931dc42ec08a5d03de95e1efe2f`
traces ID lookup in `HMInternalSetEcRamLocation`; objdump confirms setter call
at `0x9887`, table records and byte I/O at `0x5036` / `0x442e`. Analysis target
SHA remains the stock library above. This is static evidence; the subsequent
physical check establishes that the command works on this Reader.

An explicitly bounded probe wrote stock EC 0x86 to 20 then 100. The owner
confirmed the visible change. EC command readback changed from 70 to 20 to 100;
EC 0x81 changed from 198 to 148 after settling at 20. Register-derived readback
is not a calibrated luminance measurement.

`antix/backlight/reader_backlight.c` now exposes this stock behavior as Linux
`/sys/class/backlight/reader-panel`, guarded by Intel/Colbert DMI identity.
It uses the normal backlight subsystem and suspend/resume hooks; there is no
brightness daemon. Both Settings and transient idle dimming prefer this
provider. Other Linux providers remain available as fallbacks on other machines.
The module is built against the same antiX kernel headers and included in the
platform package. Existing `psb-bl`/ACPI controls are left untouched but are not
selected on the Reader once its panel driver is loaded.

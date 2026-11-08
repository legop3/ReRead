# Cooling investigation

## Initial read-only investigation

This investigates the physical Intel Reader running ReRead 0.1.0 on antiX's
`5.10.256-antix.1-486-smp` kernel. SSH access was explicitly authorized.
The embedded-controller probe is read-only: no fan settings, thresholds,
ACPI policy or controller registers were written. A short CPU load was used
to observe temperature/fan readings; it was bounded by time and conservative
temperature limits. This does not establish sustained-load thermal limits.

## ACPI temperature is a constant

The Reader exposes `acpitz` and an ACPI Fan cooling device, and the kernel's
thermal policy is enabled. Its published trips are active 50 C, passive 70 C,
and critical 95 C. However the actual DSDT method `\_TZ.TZ01._TMP` returns the
literal `0x0C00`: 3072 tenths kelvin, reported as approximately 34 C by Linux.
It does **not** read a sensor. The ACPI trip settings therefore do not prove
that Linux can detect overheating on this machine.

DSDT SHA-256:
`591d4b75b95041fb6d9cc6cbbc55e4643cc3ba6f6ed88afbd7147e368fc7fb1c`.
The captured AML and iasl disassembly are local under `build/antix/reader-dsdt.*`.
The firmware ACPI Fan methods access EC `0x82` bit 3 (`FANS`); their state is
not an RPM measurement. ACPI reported state 0 while the separate EC current
fan-speed register was nonzero. Treat these as different signals.

The kernel's `coretemp` driver also exposes a CPU-core sensor and a 90 C
critical value. It changed during the load check. The stock EC CPU reading
is a different sensor/value and differed from coretemp by roughly 10 C;
these readings must retain separate labels rather than being combined.

## Stock fan helper

`/etc/init.d/init` runs `HmColbertFan` once at boot. The pm-utils resume hook
`/etc/pm/sleep.d/00clear` runs it again, explicitly describing a workaround
for a high-speed fan defect. It is not a continuously running fan daemon.

REA/Hopper analysis of `HmColbertFan::main` (entry `0x8048870`) shows:

1. `HMInit(1)`.
2. `HMSetECRegisterVal(context, 0x36, 1)`.
3. `HMSetECRegisterVal(context, 0x38, 0)`.
4. Sleep two seconds.
5. `HMSetECRegisterVal(context, 0x36, 0)`.
6. `HMFini(context)`.

These are **library register IDs**, not physical EC addresses. Following
`HMSetECRegisterVal` through `HMInternalSetEcRamLocation` and the `ec` table
maps ID `0x36` to EC byte `0x70` (Fan Control Mode), and ID `0x38` to byte
`0x72` (Set Fan Speed). The sequence temporarily changes the mode/speed and
restores mode 0. Mode 0 being automatic, and mode 1 manual, is an interpretation
of this sequence plus the table labels; the EC firmware itself has not been
reverse engineered. The compatibility helper below uses the traced stock
sequence, not a guessed control algorithm.

The `ec` table at ELF virtual address `0x131e0` consists of 107 records of
28 bytes: physical address, bit offset, bit width, library ID, name pointer,
access flags and default value. This layout is derived from the native access
functions and ELF data. Default values in the binary are not live hardware state.

| Physical EC byte | Stock label | Initial live value |
|---|---|---|
| `0x60` | CPU temperature | 36 |
| `0x61` | Chipset temperature | 35 |
| `0x62` | System temperature | 34 |
| `0x63` | Skin temperature | 34 |
| `0x64` | U19 temperature | 32 |
| `0x65` | U20 temperature | 33 |
| `0x70` | Fan Control Mode | 0 |
| `0x71` | Current Fan Speed | 29 |
| `0x72` | Set Fan Speed | 0 |
| `0x73` | Temp to switch on fan | 34 |
| `0x74` | Temp to max speed of fan | 43 |
| `0x75` | Auto fan threshold | 1 |
| `0x76` | Fan hysteresis upper bound | 35 |
| `0x77` | Fan hysteresis lower bound | 34 |
| `0x78` | Auto fan minimum speed | 26 |
| `0x79` | Auto fan maximum speed | 58 |

Fan speed values are raw controller units. They have **not** been calibrated
as RPM or PWM percentages. U19/U20 sensor locations and which sensor drives
the fan curve remain unknown.

## Short load observation

A single Python CPU worker ran for 32 seconds. Sensors were sampled every
three seconds for about 54 seconds, including cooldown. The check would stop
at coretemp 55 C, EC CPU 60 C or EC skin 45 C; none was reached.

- EC CPU rose from 36 C to 41 C, then fell to 37 C during observation.
- Coretemp rose from 27 C to 30 C, then fell to 25 C.
- Skin stayed at 34 C.
- Fan mode stayed at 0 and reported current fan speed stayed at 28–29 raw units.
- ACPI stayed at its hardcoded 34 C.

This confirms changing EC sensor values and a nonzero fan-speed report while
Linux ACPI says the fan is off. It does **not** demonstrate a temperature-driven
fan ramp, because its reading did not materially change. The controlling sensor
and curve need a separate bounded investigation before relying on a custom
policy. No Linux fan-control daemon has been added. The observed state is
consistent with firmware automatic control, not proof of the complete policy.

## Repeatable read-only probe

The antiX kernel does not build `CONFIG_ACPI_EC_DEBUGFS`. Its exported
`ec_read()` function provides serialized access through the kernel's EC driver,
so direct I/O-port access or an old proprietary library is unnecessary.
[probe-ec.py](probe-ec.py) builds a temporary GPL module using the same prepared
headers as the ReRead release. It accepts only Intel Corporation / Colbert DMI
identity and exposes selected bytes read-only at:

`/sys/module/reader_ec_probe/parameters/snapshot`

Build after building a ReRead release, inside the builder:

```sh
python3 /work/analysis/probe-ec.py
```

For this investigation the resulting `build/antix/ec-probe/reader_ec_probe.ko`
was copied to the Reader's `/tmp`, loaded with `insmod`, and removed after
observation. It is not installed in the OS, enabled at boot or included in
release packages. It has no `ec_write()` call, setter, or raw-port access.

## ReRead stock reset compatibility (0.1.1)

The selected policy is to preserve stock cooling behavior. ReRead adds only
[fan-reset.c](../antix/fan-reset.c), a small libc-based i386 helper which
reproduces the three writes and two-second delay in `HmColbertFan::main`.
It runs once at boot inside the existing Reader startup and after resume in
`reader-power`. The embedded controller continues managing fan speed. No
sensor-based custom fan policy, periodic service or configurable fan UI is added.

Further inspection of `HMInternalSetByte` establishes that stock writes through
its SMI interface: output command `0xf4` to port `0x1201`, physical EC address
to `0x1202`, then the byte value to `0x1203`. The new helper uses exactly that
ordering, obtains permission for only those three ports, and restores mode 0
before relinquishing permission. It blocks HUP/INT/TERM during the short reset
and handles interrupted sleep. Non-Colbert DMI identity skips all I/O. Errors
obtaining permissions are logged and do not stop normal startup or resume.

`HMInit(1)` also calls a once-per-boot global hardware-default initializer.
That routine covers unrelated hardware state as well as compiled-in defaults.
ReRead does not call it or copy that global table. Its current firmware fan
thresholds and min/max settings are retained; the compatibility port covers
the fan reset itself. Full equivalence to every stock hardware initialization
side effect is not claimed.

Validation on the physical Reader:

- The non-Colbert guard skipped I/O in the build container.
- The regular package updater upgraded ReRead 0.1.0 to 0.1.1 successfully.
- A read-only EC snapshot saw mode 0 before the reset, mode 1 during its delay,
  then mode 0 after completion. Fan thresholds/min/max were unchanged.
- The Reader completed a real `deep` suspend and RTC-triggered wake through
  `reader-power`; `fan.log` recorded a successful reset after resume and mode
  read back as 0. This validates the normal Power-button helper's resume hook.
- A physical cold boot was not performed in this pass. The startup hook uses
  the existing `/run/reader-started` first-boot guard; its binary skips on
  non-Colbert hardware, so a VM cannot validate the physical fan operation.
- The temporary EC probe and uploaded installer files were removed afterward.
  Source and package changes remain in the repository; this is not a device-only
  patch. The helper, startup and resume hooks ship in `reread_0.1.1_i386.deb`.

Local result records are `build/antix/stock-fan-reset-check.jsonl`,
`stock-fan-live-result.txt`, `stock-fan-resume-check.log` and `fan-update-live.log`.

Evidence:

- `HMInternalSetByte`: `ev_44eb9b6ceb379db0a6f905fd1cde64b0456ea0fabee0c6e3c13f2e6ed063a139`.
- `HMInit`: `ev_3c68b9b7396dcfcaebe635c35c5b47e258b039a761f614ea3e8286b013c8b1ee`.
- `HMInternalSetDefaults`: `ev_2213c466a1cf4d35a70f6985a4a259786aca26114666fd2de1df24995e5dff72`.

## Evidence and limits

- Stock helper SHA-256: `5f4b1060890bf0c208432b4f047b4612ead00e3cb4316e5c6c06f47450b12775`.
- Stock hardware library SHA-256: `d04f176582b5ec6f148a06798778dfa9404161c102be5d59b509fdab2dde8200`.
- REA `main`: `ev_4185e5c14a33a991fa1f24b195709b4ea1e62c45b0b2f66a33215a952bdd8f62`.
- REA `HMSetECRegisterVal`: `ev_720bc6f3f47d0e8a72a010ea4af0056feefc8cbd88cb26f8baab14d4aa9a75e6`.
- REA `HMInternalSetEcRamLocation`: `ev_471287bcd5d275a217b696acd636618b82079931dc42ec08a5d03de95e1efe2f`.
- REA register lookup: `ev_581779b99f1f11d15315ff32699e90bd3bfd36357488d037029e7688952c05e6`.
- REA register writer: `ev_c4e1eab979f725e6fd0097518db590c9174549e860a55bf3d89d4a1736f65e35`.
- REA fan strings: `ev_9e3d82324a65a6713964cf98be9ec2975b1d2911b529e1dc5bcd781c3f68dcd0`.
- Native pseudocode is provider output, not original source; Hopper does not
  expose complete function-body ranges. Address/argument checks used assembly.
- Local runtime records: `build/antix/hardware-cooling-live.txt` and
  `build/antix/cooling-load-check.jsonl`.
- Hardware shutdown, fan tachometer accuracy, long-load cooling and the stock
  high-speed resume defect remain unverified. No claim that critical shutdown
  works follows from merely finding the configured ACPI trip.

Kernel interface references:
[thermal sysfs](https://docs.kernel.org/5.10/driver-api/thermal/sysfs-api.html)
and the [Linux 5.10 EC debug driver](https://github.com/torvalds/linux/blob/v5.10/drivers/acpi/ec_sys.c).

# Battery and charging

> Historical investigation and ReRead 0.1.2 implementation. ReRead 0.10.0 replaces
> the custom policy with UPower; see [current integration](linux-battery-apps.md).

## Physical observations

The Intel Reader uses its standard ACPI battery and AC adapter devices under
antiX `5.10.256-antix.1-486-smp`. The internal pack identifies as Intel HC01,
7.4 V nominal, with 3,300 mAh design capacity and 3,541 mAh reported full capacity.
These are gauge reports, not independently measured battery health.

During the check, the user disconnected and reconnected only the DC adapter,
leaving the battery and USB connections in place:

- Initially: AC online 1, capacity 97%, remaining 3,449 mAh, voltage 8.307 V.
- Unplugged: AC online changed to 0. Remaining charge declined and the displayed
  percentage changed from 97% to 96%, confirming changing gauge data.
- Reconnected: AC online returned to 1. At 96%, charging remained paused and
  the gauge reported about 3,410 mAh and 8.267 V, later 8.277 V.
- Battery removal was not needed and was not performed.

The stock help text `parts/top/top.xml` explicitly allows a full-charge LED
indication at 95–99%. Near-full charging pause is consistent with the observation,
but a depleted-to-full active charging cycle was **not** demonstrated here.
We do not infer that a charging fault is impossible merely from AC being present.

## Firmware status quirk

The captured DSDT's AC adapter `_PSR` returns the actual EC AC-presence bit.
Battery `_BST` reads remaining charge and voltage from EC-backed fields.
Its status logic reports Discharging when `BCHA` is zero, Charging otherwise,
then clears the status when the battery-full flag is asserted. Thus an external
power source and a non-charging battery can legitimately appear together as
`AC/online=1` and `BAT0/status=Discharging` in Linux on this Reader.

ReRead shows **Not charging** for that combination, while keeping `raw_status`
and `external_power` separate in its API. It does not manufacture a Full status
from a 96–97% reading, nor treat Discharging alone as proof the adapter is absent.

`_BST` leaves present rate at the unknown sentinel. `BAT0/current_now` returns
ENODEV, so ReRead reports null current and does not estimate remaining runtime.

## Stock comparison

The stock hardware library's `ec` table defines capacity warning/critical
threshold defaults of 400 mAh and 200 mAh. `HMGetBatteryCapacityThreshold`
converts a threshold into a percentage of the reported pack capacity. For the
current full-capacity report that is approximately 11.3% and 5.6%.
These are recovered library defaults, not a verified complete policy of the
stock application's settings or shutdown timing.

ReRead uses warning 12% and critical 6%, with a 30-second critical grace period.
These approximate the stock reserve thresholds with some rounding headroom;
the grace period is ReRead policy. No EC charge-control register, charger limit,
or vendor global initialization table is changed.

## ReRead 0.1.2 implementation

[antix/battery.py](../antix/battery.py) discovers the internal battery and
external power supply by Linux power_supply types rather than BAT0/AC names.
A small runit `reader-battery` service samples every five seconds. It writes
current status to RAM at `/run/reader-battery.json` and a readable line at
`/run/reader-battery-status`; persistent logs record state transitions in
`/var/log/reader/battery.log`. The Reader's diagnostic screen displays that line.
The web Overview shows battery state, and low/critical notices are visible
across web pages. Audio alerts remain future work alongside audio support.

`GET /api/battery` exposes:

- `present`, `external_power`: true/false, or null when unavailable.
- `capacity_percent`, `status`, `raw_status`.
- `voltage_uv`, `charge_now_uah`, `charge_full_uah`, `current_ua` (null if unavailable).
- `level`, `shutdown_in_seconds`, `monitor_running`, `sampled_monotonic`.

The API uses a fresh monitor sample or returns live readings with
`monitor_running=false` when the service has stopped. Apps can also read the
RAM JSON without depending on the web server. No battery credentials/data are
included in packages. The service, code and default config ship in the same
`reread` package used for installations and updates.

Editable dpkg conffile `/etc/reader/power.json`:

```json
{"warning_percent":12,"critical_percent":6,"shutdown_delay_seconds":30}
```

Restart `reader-battery` with `sv restart reader-battery` after editing. Invalid
configurations are logged and fall back to the defaults, without replacing the
file. Its customized contents are preserved by platform package upgrades.

Critical shutdown requires a present battery, known capacity at/below the
critical threshold, and confirmed external power absence continuously through
the grace period. Reconnecting external power, losing a required reading, or
recovering charge resets the countdown. The service rechecks live readings
immediately before calling antiX's normal `/sbin/poweroff` path. Missing/unknown
readings and absent batteries do not trigger shutdown. A failed power-off gets
a new grace period before retrying. Charging regulation remains in hardware.

## Validation and limits

- Actual adapter removal/reconnection and changing capacity/voltage verified.
- Normalized live HTTP status, installed service and on-device status file verified.
- The original test-battery.py (since replaced with UPower adapter tests) covered absent/unknown data,
  percentage fallback, charging/discharging interpretation, warning/critical
  thresholds, grace timing, recovery, and AC reconnection at the final recheck.
  It mocks the power-off command while asserting the actual command/arguments.
- The Reader was not drained to 6% or powered off by the new monitor. Physical
  low-battery shutdown and a complete active charging cycle remain unverified.
- Protection runs while the OS is awake. Userspace does not run during S3;
  periodic wake or low-battery protection during long sleep is separate work.
- There is one internal battery on the Reader. Multi-pack aggregation is not
  implemented; the first reported-present internal battery is used.

Local runtime records are `build/antix/battery-{initial,unplugged,reconnected}-live.txt`,
`battery-update-live.log` and `battery-installed-live.txt`.

REA/Hopper evidence for the stock `libcolbert-hm.so` (SHA-256
`d04f176582b5ec6f148a06798778dfa9404161c102be5d59b509fdab2dde8200`):

- Battery presence: `ev_263e497212af57a28bd5da5278e1996ae244e2ae9d1331adaf497a44d0a0f0e6`.
- Remaining charge: `ev_2b890f06a55838ba8f0954cfe63308dfe6cec9395c3616d051237d74adc15b2c`.
- AC power status: `ev_083afe5f845c278ab4e0ce098e975f756775fbb6baae345a5ca6bffbf928fb23`.
- Threshold conversion: `ev_574d927106e2272a7485c740d6d67d4cfeace57b6ec990339509ddd897aacc6a`.
- Battery strings: `ev_aee25213ca13d4c90c94641a11c3f3b0a2f62fbe8b20d0d7003f04fe53e40af4`.

Pseudocode is provider reconstruction, not original source. Hopper does not
report complete function-body ranges. Register IDs/defaults were recovered
from the ELF `ec` table as described in [cooling.md](cooling.md).
Linux units and property meanings follow the
[power_supply interface](https://docs.kernel.org/power/power_supply_class.html).

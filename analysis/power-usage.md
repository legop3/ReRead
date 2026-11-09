# Idle battery draw — ReRead 0.10.0

Measured on the physical Reader on 2026-11-09, 12:09–12:14 EST, after a one-minute
settling period. No Reader settings were changed for this test.

Conditions: AC adapter disconnected (kernel AC online=0 throughout), battery
discharging, Home idle, backlight already dimmed to 10%, no camera stream,
auto-suspend Off. USB management and Wi-Fi remained connected. The owner was
asked to close the management web tab and leave the device idle. A single SSH
collector sampled counters once per minute; it did not poll the web API.

## Results

| Measurement | Five-minute result |
| --- | --- |
| Raw charge consumed | 44 mAh (3,520 to 3,476 mAh) |
| Reported capacity | 99% to 98% |
| Estimated battery energy consumed | 0.365 Wh |
| Estimated average battery draw | **4.38 W** |
| Total CPU busy time | **0.58%** across both logical CPUs |
| Kernel-reported C6 residency | **96.72%** of logical CPU time |

The battery does not expose current_now or power_now. Energy was estimated by
multiplying each minute's charge loss by that interval's mean measured battery
voltage, then summing. This is a battery-gauge estimate, not a wattmeter reading.
One-minute estimates ranged from 3.98 to 4.48 W, consistent with charge-counter
quantisation. USB remains attached, so this is not a completely untethered test.

## Process CPU comparison

Percentages below use **100% = one logical CPU**. Earlier figures come from the
30-second connected sample in `build/antix/goal-idle-resources.json`, recorded
before the standard-service migrations. These are separate observations, not a
controlled old/new software A/B experiment.

| Process/group | Earlier | Current quiet five-minute run |
| --- | ---: | ---: |
| Home | 0.232% | 0.180% |
| Battery handling combined | 0.332% | 0.400% |
| Session adapter | Not separate | 0.063% |
| D-Bus | No CPU ticks recorded | 0.110% |
| Web server | Not confidently identified | 0.043% |

Current battery handling consists of the UI adapter (0.203%) and UPower (0.197%).
Elogind and triggerhappy recorded no CPU ticks in this window. These numbers are
much lower than the earlier busy 20-second spot check, where battery handling
was 1.19% and total CPU was 2.28%. Quiet conditions materially affect the result.

The current idle CPU cost is small. There is no pre-migration battery-discharge
measurement, so the wattage change caused by the migration remains unmeasured.
Do not extrapolate a precise full-charge runtime from this short, near-full test.

Ignored raw samples, collector and calculation script:
`build/antix/power-usage/discharge.jsonl`, `discharge.py`,
`summarize-discharge.py`, and `discharge-summary.json`.

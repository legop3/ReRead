# Wayland trial on the Intel Reader — 2026-11-09

Temporary on-device trial against ReRead 0.16.0, antiX kernel
5.10.256-antix.1-486-smp, Atom Z520, 485 MiB usable RAM. The production
service and installed binaries were not modified. The same Qt Widgets apps
were switched from LinuxFB to Wayland with Weston 14.0.2 kiosk-shell and
Pixman software rendering. The existing LVDS-1 960×544 mode was retained.

## Result

Keep the current Qt/LinuxFB backend for now. A compositor is architecturally
useful, but this trial exposes driver compatibility problems and additional
camera-preview CPU work. This is not evidence that Wayland is
inherently too expensive; it is evidence against this particular stack on this
Reader with its current kernel/graphics driver.

| Workload | Qt/LinuxFB | Qt/Wayland + Weston |
| --- | --- | --- |
| Idle Home, total machine busy | about 1% | about 1% |
| Camera preview, total machine busy | about 94% | about 98% |
| Backend reported preview rate | initial spot check 15 fps; repeat 9 fps | about 9–10 fps |
| Weston during camera preview | absent | about 9% of one logical CPU |

CPU samples were three consecutive 10-second `/proc/stat` and per-process
`/proc/PID/stat` deltas per condition. Total-machine percentages include both
logical CPUs; process percentages use one logical CPU as 100%. Camera rates
are the native camera backend's `preview_fps`, not independently measured
panel presentation rates. These are short practical measurements, not battery
runtime tests. Samples overlapping package installation or compositor failure
were discarded. The restored baseline also reported about 9 fps, so the initial
15 fps spot check does not establish a Wayland frame-rate penalty. No controlled
on-screen frame-rate comparison or thermal/frequency control was performed.

## Compatibility findings

- Weston starts on gma500drmfb with legacy KMS and Pixman, but unmodified Weston
  aborts in `weston_output_finish_frame`: its timestamp monotonicity assertion
  fails. The preceding repaint-delay warning is approximately minus uptime.
- A temporary libdrm callback interceptor confirmed zero page-flip event
  timestamps. Substituting monotonic receipt time for zero timestamps let
  Weston run for the valid measurements. This is a diagnostic workaround,
  not a production driver fix or validated vblank timing solution.
- Cage 0.2.0 / wlroots 0.18.2 refuses to create its DRM backend: `PRIME import
  not supported`. Changing renderer to Pixman does not bypass that requirement.
- A temporary shared-helper variant disabled each app's raw evdev reader under
  Wayland and translated Qt native scan codes back to existing semantic buttons.
  The user confirmed Home looked normal and physical direction buttons worked.
  Home → Files → Home → Camera switching also worked through Wayland input.
- Qt's default Wayland client integration unnecessarily initialized Mesa/LLVM
  even for these raster Widgets apps. Forcing the client integration to `none`
  avoids that initialization and uses the raster shared-memory path, with a Qt
  warning about the unavailable integration. This reduced memory use, but did
  not remove the camera CPU bottleneck. It is another trial setting, not a
  polished deployment configuration.
- Increasing Weston's repaint window from 7 to 34 ms also failed to restore
  the initial 15 fps backend spot reading; however, the restored baseline also read
  about 9 fps, so that comparison is inconclusive.
- Debian's Weston plus Qt Wayland packages added 124 packages / about 245 MB
  on this installation; Cage added another 18 / about 10 MB incrementally.
  Those figures include dependencies already present from the preceding trial;
  the Cage increment is not a clean-install dependency footprint.

Temporary scripts, modified helper libraries, the timestamp interceptor source,
and raw measurements are under ignored `build/wayland-trial/`. No release,
installer, or production rendering code was changed. A production switch would
need a real graphics compatibility solution, a repeatable performance win or
acceptable tradeoff, and sleep/wake/input lifecycle validation. Those are not
established by this trial.

## Recovery

The original supervised UI was restored and remeasured. Camera preview again
used about 94% total CPU (92.8–94.4% samples). Its later backend rate was about
9 fps, which is why the apparent initial frame-rate regression is inconclusive.
The 142 packages newly installed across both trials were explicitly identified
from APT logs; a simulated purge confirmed no pre-existing packages would be
removed. All 142 trial packages were then purged. Download cache was cleared;
production binaries, boot configuration, user files and settings were retained.

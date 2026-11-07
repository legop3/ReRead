# Stock Xorg log photographs — 2026-11-07

Sources: `evidence/20261107_000917.JPG`, `20261107_000924.JPG`, and `20261107_000927.JPG`. These show the user's filtered `/var/log/Xorg.0.log` output on the Reader in its recovery shell. They are recorded log evidence, not a measurement of a currently running X session.

The first photograph shows:

- `Found panel mode in BIOS VBT tables` followed by a modeline named **960x544**.
- A warning that BIOS panel mode data does not match probed data, followed by continuation with probed data.
- Additional **960x544** modelines, with printed clocks **15.66** and **15.02** MHz.
- `Output LVDS0 using initial mode 960x544`.
- A separate clock-programming debug message reporting `chosen: dotclock 20000`. Do not assume this is the same thing as the printed modeline clock or that all lines describe one final mode.

This refines the earlier configuration-only finding: `xorg.conf` requests **962x544**, but this recorded Xorg initialization selects **960x544**. Neither value should be treated as the definitive active hardware timing without checking the full log or querying a working X session.

The other photographs mostly show repeated panel-power and clock-register debugging.

## Focused modeline photograph

`evidence/20261107_001647.JPG` shows the result of `grep -i modeline /var/log/Xorg.0.log`. Visual transcription:

```text
Modeline "960x544"x0.0 15.66 960 968 971 987 544 549 552 560
Modeline "960x544"x0.0 15.66 960 968 971 987 544 549 552 560
Modeline "960x544"x0.0 15.02 960 968 971 987 544 549 552 560
```

The lines wrap to printed horizontal frequencies of about 15.9 and 15.2 kHz, respectively. These are photographed rounded values, not a machine-readable export. No sync-polarity flags are visible. Neither modeline has yet been tested on a replacement kernel, and the photograph alone does not resolve which clock was ultimately programmed. Preserve both candidates alongside the earlier separate dotclock debug message.

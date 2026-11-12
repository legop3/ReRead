# Guide, Favorites and system status (ReRead 0.25)

One shared Screen implementation owns Guide and Favorites. Guide is strictly
hold-to-show; it works with no app hints and includes a common Close app hint.
Favorites uses normal Qt close handling and is not delivered to app navigation.
Home's internal property is its sole exception, returning to the Home grid.
A single status component supplies Home's header and Guide's bottom row.

UI 5's public class layout and virtual interface are unchanged. Existing app
binaries therefore gain these rules with the platform package; recompiling every
unchanged app/version is unnecessary. Legacy redundant Favorites handlers are
unreachable through Screen. J2ME 0.3.5 also fixes its explicit old-SDK fallback and
hint labels: Favorites closes the app; its View menu retains Exit to Java ME.

## Status and resource use

Battery/charging comes from existing UPower-backed reader-battery JSON, volume
from the existing ALSA status file, free storage from Qt filesystem statistics,
and Wi-Fi association/SSID from asynchronous, time-bounded `wpa_cli status`.
The wireless interface is detected from sysfs, matching the current network
setup; there is no adapter-specific name or Wi-Fi scan. Refresh is every five
seconds only while the relevant foreground status is displayed. It stops for
background or dimmed windows and never sends activity or acquires an inhibitor.
No new daemon, IPC protocol, rendering service or device permission mechanism.

Battery retains charge-dependent green/amber/red. Connected Wi-Fi is cyan,
volume yellow (neutral when muted), and storage white, amber below 10% available
or red below 5%. All badge text is black; time stays white on black.

## App audit

| App | Closing / contextual help |
| --- | --- |
| Camera | Shared Guide; Favorites closes the client. Captures are owned by the service worker, so closing the client does not cancel a committed capture. Back retains its capture-in-progress guard. |
| Files | Shared Guide; normal window close; no background worker to strand. Folder/actions hints remain contextual. |
| Software | Shared Guide; closes its UI. Package jobs belong to the existing backend and continue independently. |
| Images | Shared Guide; Qt animation is destroyed on app exit; information, rotation and zoom modes retain their hints. |
| Text | Shared Guide; normal close; no persistent worker. |
| Audio | Shared Guide; closeEvent stops playback and releases its idle inhibitor; destructor releases GStreamer resources. |
| Booru | Shared Guide; normal close and destructor cleanup; cards/settings/viewer contexts retain their hints. |
| Internet Archive | Shared Guide; normal close and destructor cleanup reviewed in source. Not installed on the test Reader. |
| Java ME | Shared Guide during gameplay/menu; Favorites closes the app and its Java child. View's game menu owns return-to-library. |

## Validation

- Expanded shared help test: press/release, repeats, child editor focus, focus
  loss, sleep clearing, empty hint lists, rotated safe bounds, central close and
  Home's exception.
- Reader: Guide and Favorites tested in all eight installed apps above. Home
  remains running. Shared status and saturated backgrounds inspected from actual
  framebuffer captures (kept only in ignored build output).
- Reader: launched a game, showed/released Guide, then Favorites; GUI exited with
  success and its child processes disappeared.
- No physical suspend/wake or new full UI screenshot comparison is claimed by
  these checks. The older VM color-based UI assertions still need refreshing.

# Java ME for ReRead

ReRead's Java ME app is a game library and an **embedded renderer**, not
a launcher for a second graphical Java window. It uses FreeJ2ME-Plus's
existing headless/libretro core: the Java process receives phone-key packets
and sends decoded RGB LCD frames to this app. The **only** visible window is
the shared ReRead `Screen`.

## Build

From the ReRead checkout on Fedora:

```sh
git pull
docker run --rm -v "$PWD:/work:z" reread-builder \
  python3 /work/antix/apps/j2me/package.py
```

Output: `out/antix/reread-app-j2me_0.3.2_i386.deb`.

This **single command** compiles the i386 Qt app, fetches pinned upstream
FreeJ2ME-Plus source, builds its `freej2me_plus-lr.jar` headless core with
Ant, and bundles it in the app package. No game JAR, external emulator JAR,
local Java build, second Docker mount, or extra OS image build is required.
The Reader uses the existing `openjdk-21-jre` package and never needs
compiler tools or Ant. The builder installs its own temporary build tools
when needed.

## Game library

Put any compatible Java ME `.jar` files in **Files/J2ME**, or upload them
through the web file manager to `/var/lib/reader/files/J2ME/`.

Open **Java ME** from Home. Browse the games by D-pad; press OK to play.
JAR manifests provide game title/vendor/version; the library updates when
files change. No installation or compilation of game JARs is required.
The app saves per-game preferences and emulator progress separately under
`/var/lib/reader/apps/j2me/`. Games and saves survive app upgrades.

Each game has button-adjustable options: virtual phone screen width/height,
actual 0°/90°/180°/270° rotation, phone type/key mapping, frame cap,
reset settings, and confirmed erasure of saved progress.

## In-game controls

| Reader control | In game |
| --- | --- |
| D-pad | Phone directions, rotated to match the game display (0°/90°/180°/270°) |
| OK | Phone Fire / Select |
| Back | Right phone softkey |
| Options | Left phone softkey |
| Zoom + / - | * / # |
| View | Open the ReRead game menu |
| Favorites | Close the game, return to the library |
| Guide (hold) | Standard ReRead contextual help overlay |

The game menu has **Resume** and **Exit to Java ME**. It is painted by the
ReRead app itself. Guide is never overloaded with another function; it
remains hold-for-help, release-to-hide, with standard icons and context labels.
Other hardware keys such as Power, Hold and volume remain system-owned.
Keys held during menu entry are released to prevent stuck game input.

The build patches the upstream headless core so MIDlet startup runs on a separate thread rather than blocking its input/frame protocol reader. The host uses the actual libretro key indices (0–3 directions, 7 fire, 8/9 softkeys, 12/13 star/pound), not the standalone AWT key indices. These address the observed one-frame/ignored-buttons regression.\n\nThe Java process is controlled with nonblocking stdin/stdout packets. ReRead
alone owns input focus, Game and Menu screen painting, Guide/Help overlay,
and normal window management. Rendering stays in the standard
`Screen::content()` area with 14-pixel safe insets, black background and
aspect-preserving scaling. No AWT frame, X11 embedding, alternate window,
raw evdev input, framebuffer writes, or second GUI window is used.

## Java encoding, storage and failures

The headless core starts with `-Dfile.encoding=ISO_8859_1` to avoid the
unexpected subprocess restart seen with the previous AWT frontend.
A 30-second startup watchdog reports a failure if no frame arrives.
Recent Java stderr is bounded to 16 KiB for UI errors and written to each
game's `last-run.log`.

When the game exits or fails, the same ReRead screen returns to the
library or displays an error. Its process is stopped and reaped.
Uninstalling/updating the Debian package does not erase uploaded game JARs
or private saves. A smaller bundled i386 Java runtime is possible in the
future, but this package retains the proven installed JRE.

## Validation

The original *AWT* FreeJ2ME frontend and Peggle JAR have been verified
on actual Reader hardware. The revised **headless frame-pipe integration**
has not yet completed an end-to-end package build/hardware test.
Treat it as a new integration until the on-Reader test confirms rendering,
input, audio, rotation, saves, Guide, pause/resume, and exit. Do not
mistake the earlier working AWT experiment for validation of this path.

Implementation follows `APP_DEVELOPMENT.md`, the project's definitive
ReRead app contract.

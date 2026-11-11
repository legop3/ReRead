# Java ME for ReRead

A general-purpose game and application library for classic J2ME/MIDlet JARs.
This is not a Peggle-specific app: any compatible J2ME game can live alongside
the others in one folder. The emulator is FreeJ2ME-Plus, built from its pinned
source revision as part of this app's independent package build.

## Build the standalone ReRead package

From the ReRead checkout on Fedora:

```sh
git pull
docker run --rm -v "$PWD:/work:z" reread-builder \
  python3 /work/antix/apps/j2me/package.py
```

Output: `out/antix/reread-app-j2me_0.2.1_i386.deb`.

One command performs everything: compiles ReRead's i386/Qt launcher, installs
any missing Java compilation tools **inside the disposable builder**, checks
out a pinned upstream FreeJ2ME-Plus revision, runs Ant with modern Java 8
source compatibility, integrates Reader keypad mappings and rotation into FreeJ2ME itself, includes licenses, and
assembles the .deb. No game JARs, external --emulator flag, or separate build
mount are required. Internet access is required on the first build.

The installed app depends on Debian's `openjdk-21-jre` **runtime only**. The
Reader does not receive Ant, Git, Java compiler tools, or source checkouts.

## Normal use

1. Put mobile Java `.jar` files in the **J2ME** folder in ReRead Files
   (`/var/lib/reader/files/J2ME/`). This is the *only* game library.
2. Open **Java ME** on the Home screen.
3. Browse the games using the D-pad. Titles, vendors and versions are read
   from MIDlet JAR manifests (without executing the JAR).
4. Press **OK** to start the selected game directly in FreeJ2ME-Plus, normally
   fullscreen. Games are not installed individually.
5. Press **Options** on a selected game to customize it, or toggle the
   settings page closed to return to the library.

The game folder is watched for changes; the list refreshes when JARs are
added or removed. Additional game-specific state lives under
`/var/lib/reader/apps/j2me/`, not in the uploaded JAR folder.

### Per-game settings

- LCD rotation: 0°, 90°, 180°, 270° via FreeJ2ME's actual renderer
- Emulated LCD width and height (independent of device orientation)
- Frame-rate limit
- Phone keypad layout: Standard, LG, Motorola, Moto Triplets, Moto V8,
  Moto A1000, Nokia, Sagem, Siemens, SKT or KDDI
- Fullscreen on/off and windowed scaling
- Reset game settings to defaults
- Delete a game's emulated save/data files, behind an explicit confirmation

Settings are persistent, stored atomically, and separated by game. Game saves
and configuration live in isolated per-game working directories. The library
tracks launches/last-played time. Runtime stdout/stderr is written to each
game's `last-run.log`; a failed launch presents the captured output on the
device rather than hiding it behind an exit code.

### Physical button controls

| Reader | J2ME |
| --- | --- |
| D-pad | Phone directions / 2,4,6,8 under Standard layout |
| OK | Fire / 5 |
| Back | Right softkey |
| Options | Left softkey |
| Zoom + | * |
| Zoom - | # |
| Hold Guide (?) + Back | Exit the running game |

The mapping is compiled **into the FreeJ2ME Java frontend** during the package build. FreeJ2ME runs through its standard manifest entrypoint (the same path as the hardware-tested direct launch). ReRead's
system-wide keyboard mappings, power, volume and Hold handling are unchanged.
Gamepad/key compatibility varies with the phone profile selected.

### Storage / maintenance

Removing a game JAR from the Files/J2ME folder removes it from the library
without deleting its saves. A game's saved data can be explicitly erased from
its settings. Uninstalling the ReRead package does not automatically erase
user game files or saves. If manually removing the old early hardware test,
`/opt/peggle-test` is separate from this app and isn't used by it.

### Java runtime size

A custom i386 `jlink` runtime limited to `java.base`, `java.desktop` and
other actually used modules is possible. It must be built using **i386 Java
modules** and tested for native font/audio/X11 dependencies. This release
still depends on the already-working packaged OpenJDK 21 runtime, rather than
silently replacing it with an unverified cross-architecture bundle.
Disk savings will be quantified before changing that dependency.

## Release validation

FreeJ2ME-Plus and Peggle J2ME **have** run successfully on the actual Reader.
The full library package, compiled keypad adapter, and rotation/exit mapping
**have not** yet been verified in an end-to-end device install. Do not treat
that separate hardware validation as completed. Test build, install, JAR
discovery, fullscreen, physical softkeys, rotation, saves and safe game exit
before calling the entire app production-tested.

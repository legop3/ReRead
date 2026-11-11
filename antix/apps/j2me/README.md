# Java ME runner for ReRead (first integration build)

General-purpose FreeJ2ME-Plus frontend. This is **not** a Peggle-specific app.
It uses the actual FreeJ2ME-Plus AWT JAR already tested on Fedora and Reader,
without bundling proprietary game files.

## Build

From the ReRead checkout on Fedora, run the same single-app builder command as
for Booru:

```sh
docker run --rm -v "$PWD:/work:z" reread-builder \
  python3 /work/antix/apps/j2me/package.py
```

Result: `out/antix/reread-app-j2me_0.1.1_i386.deb`.

This build is **self-contained**: `package.py` installs the Java compilation
tools in the disposable Docker build container if needed, fetches a pinned
upstream FreeJ2ME-Plus revision, compiles its standalone AWT JAR with Ant,
compiles the ReRead-specific keypad adapter from source, then packages them
with the ReRead launcher. Network access is required for the first upstream
checkout and Debian build packages. Nothing from `~/freej2me-plus` is used.
No JDK, Ant, or Git is installed on the Reader.

The app depends on the Reader's `openjdk-21-jre` package, already confirmed
capable of running FreeJ2ME-Plus. A compact bundled i386 runtime is a
separate optimization; it is **not** claimed to be included yet.

## Usage

- Upload supported Java ME game `.jar` files to **Files/J2ME**. The folder is created automatically.
- Open **Java ME** from Home. Browse the JARs in that folder and press OK to launch.
- On the selected game press **Options** for game-specific settings:
  orientation (portrait/landscape virtual LCD), virtual screen width/height,
  frame cap, and phone key layout.
- Each game gets isolated FreeJ2ME save/config data under
  `/var/lib/reader/apps/j2me/data/<id>`; settings under `profiles/<id>.json`.
  JARs themselves stay in Files.
- Fullscreen is requested via FreeJ2ME's supported command-line arguments.
  The AWT frontend decides its actual scaling and aspect ratio; the first
  Reader package needs testing for correct fullscreen sizing.

### Reader controls

The included `ReReadJ2ME` Java 8 bootstrap translates key events **inside
the emulator process only**, leaving the OS's keymap untouched. Its mapping
is based on FreeJ2ME-Plus's documented AWT default layout:

| Reader button | FreeJ2ME desktop key | Default phone key |
| --- | --- | --- |
| D-pad | Arrow keys | 2 / 4 / 6 / 8 |
| OK | Enter | 5 / select |
| Back | W | Right softkey |
| Options | Q | Left softkey |
| Zoom + | E | * |
| Zoom - | R | # |

Other keys remain owned by ReRead or by the original emulator. The Java
input adapter's handling of Reader special multimedia keys **has not yet been
verified on the actual device**. FreeJ2ME's phone layout option can change
what directional and enter keys mean.

The game runs in a separate AWT window. The Home/Favorites key may be consumed
by ReRead's own global shortcut handling; stop/return-from-game behavior is
not yet confirmed and needs a hardware check.

### Orientation semantics

Portrait/landscape currently swaps the **virtual phone LCD dimensions** on
a per-game basis. It does not rotate ReRead's hardware display or framebuffer.
A true 90/270-degree rotation of the running image is a later feature and
must not be confused with this setting.

## Slim Java runtime

The first release intentionally uses the already-working Debian i386 OpenJDK
21 runtime. A reduced **i386** runtime may be possible with `jlink` and
`java.desktop` plus runtime dependencies. It must be produced using i386
JDK modules, not an amd64 Fedora runtime, and tested for FreeJ2ME sound and
graphics before substituting it for the Debian package.

Do not remove the working OpenJDK package until a smaller runtime has been
produced and tested. The full JDK and Ant are not required to run the app.

## Validation status

This is first-pass source integrated into the ReRead repository. Direct
FreeJ2ME-Plus + Peggle has been verified on the Reader; the new launcher,
packaged keypad remapper, and fullscreen configuration have not yet been
verified on actual hardware. No claim of tested full integration is made.

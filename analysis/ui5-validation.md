# Shared UI 5 and button artwork

ReRead 0.20 moves Guide ownership into `libreader-ui.so.5`. Applications supply
contextual `controls`; they no longer manage footer visibility or intercept
Guide. The removed flags and changed Screen layout require rebuilt applications.
Every bundled app declares a minimum core of 0.20, and the core breaks older
bundled app versions so APT upgrades them together.

Help is hidden normally, overlays content while Guide is held, and disappears on
release. Repeats do not toggle/reopen it. Focus loss, hiding, dimming, sleep and
Hold lock clear it. The child overlay is raised over ordinary child widgets,
wraps long lists at readable size, and rotates/repositions with inverted UI.
Content geometry never reserves a footer. Hidden help does no layout or drawing
work and adds no timer or service.

All eight apps and Home/Settings use this shared behavior. Archive's private
footer renderer is removed; it uses the shared palette and list/info layout for
its file collection. Text and the example no longer duplicate Guide handling.
Contextual hints account for document information and errors. The application
guide and starter now describe implemented APIs rather than pending rules.

The shared hicolor SVGs now use an empty OK circle, northeast Play triangle,
thick Voice Speed down arrow with speed lines, and tailless directional points.
Button names, semantic IDs, scan codes and Linux/XKB assignments are unchanged.
`buttons.json`, the generated reference sheet and physical table agree.

## Validation

- Shared help tests cover press/release/repeat, content restoration and stable
  geometry, child-widget input/stacking, focus/sleep/hide cleanup, wrapping and
  inverted placement. They use normal Qt events and the shared Device adapter.
- Existing Files, Images and Text checks cover navigation, MIME dispatch,
  deletion, zoom/pan/rotation, Unicode rendering and bounded previews.
- Core, all eight app packages and the developer example are built for i386.
  Their executables target UI SONAME 5. Generated input files and documentation
  links are checked; the corrected SVG reference is visually inspected.

On the Reader, the matched core and all eight packages installed successfully,
with a clean dpkg audit. Xorg key events showed the Guide overlay in Home and
all eight apps; releasing restored the previous content (live Camera/Audio
content was allowed to advance). Every app closed normally back to Home.
Framebuffer captures confirmed the corrected icons, shared high-contrast overlay,
Archive palette and inverted Camera help placement. Audio used a silent WAV;
this was an input/UI check, not a new speaker or codec validation. No photos,
account mutations, package operations through Software, or suspend cycles were
triggered by these checks.
This change does not alter camera capture processing, system power policy,
permissions, networking or file-opening conventions. The downloadable installer
image remains 0.17; fresh source builds use the current core and default apps.

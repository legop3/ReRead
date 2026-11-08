# Intel Reader button recovery

Investigated 2026-11-07 from the owner's stock disk image. The SSD-root POC is now confirmed to install and boot on the Reader. **Physical scan measurements now replace the earlier incorrect inferred map.**

## Hardware-calibrated map

The owner's [raw readings](../buttons.md) supersede the static reconstruction below. Several controls emitted only `MSC_SCAN`; the earlier claim that all had usable key events was incomplete. The corrected viewer assigns every measured scan a unique Linux keycode, including replacing earlier incorrect assignments on the internal AT keyboard. USB keyboards are unaffected. Assignments are reapplied at boot without EC writes.

[`antix/reader-keys.h`](../antix/reader-keys.h) is the single code definition. Future applications should use its `READER_*` IDs for physical controls and treat Linux codes as transport values. These IDs describe the physical controls; an app may give them different actions. Values already defined in the enum should remain stable as controls are added. On the internal keyboard, scan and key lookups share that table. ACPI Power can also arrive independently as Linux code 116; applications should normalize that to `READER_POWER`.

| Physical control / stable ID | Measured scan | Previous displayed code | Corrected Linux code |
| --- | --- | ---: | ---: |
| Shoot / `READER_SHOOT` | `0x66` | 141 | 212 |
| Hold / `READER_HOLD` | `0x65` | scan only | 152 |
| Power / `READER_POWER` | `0xde` | 116 | 116 |
| Play (blue arrow) / `READER_PLAY` | `0xa2` | 164 | 164 |
| Back (diagonal arrow) / `READER_BACK` | `0x63` | scan only | 158 |
| D-pad Up / `READER_UP` | `0x6c` | scan only | 103 |
| D-pad Down / `READER_DOWN` | `0x6d` | scan only | 108 |
| D-pad Left / `READER_LEFT` | `0x6e` | scan only | 105 |
| D-pad Right / `READER_RIGHT` | `0x6f` | scan only | 106 |
| Center / `READER_OK` | `0x1c` | 28 | 28 |
| Menu / `READER_OPTIONS` | `0x71` | scan only | 139 |
| Minus / `READER_ZOOM_OUT` | `0x6a` | 151 | 418 |
| Two rectangles / `READER_VIEW_TOGGLE` | `0x6b` | 153 | 227 |
| Plus / `READER_ZOOM_IN` | `0x69` | 146 | 417 |
| Record-like symbol / `READER_FAVORITES` | `0x68` | scan only | 156 |
| Speed arrow / `READER_VOICE_SPEED` | `0x67` | scan only | 208 |
| Question mark / `READER_GUIDE` | `0x01` | 1 | 138 |
| Volume Up / `READER_VOLUME_UP` | `0xb0` | 115 | 115 |
| Volume Down / `READER_VOLUME_DOWN` | `0xae` | 114 | 114 |

The last three front-panel names follow the stock manual's physical layout; the measured symbols and scans are recorded alongside them. Both right-edge volume controls are now measured and retain their existing Linux keycodes. All 18 push buttons and the Hold slider have recorded scans.

Hardware verification confirms Hold is reported as a maintained key: `EV_KEY` code 152, value **1 (pressed) = unlocked**, value **0 (released) = locked**. Treat this as switch state, not a toggle on each event. Initial state reporting at boot has not been checked. Hold is observed without enforcing a lock. Power press/release now requests suspend-to-RAM.

The owner verified that all physical button labels are correct on the Reader. `Mapped: 19/19` reports successful ioctl assignments (including already-correct entries); `Bindings` counts entries changed during startup, not physical buttons. QEMU can verify these ioctls and ordinary keyboard/ACPI events, but cannot reproduce the custom Reader scans.

Logs persist on SSD at `/var/log/reader/buttons.log` and `/var/log/reader/input-devices.log`. Reinstallation replaces them.

## Historical static reconstruction — superseded

**Do not use the following table for physical control mappings.** The hardware measurements above disproved this inferred translation. It is retained only to document the original investigation.

`AT scan` is the translated scancode index accepted by evdev's keymap ioctl. It is **not** the SDL/Xorg code. Extended AT codes use bit 7 here: for example, `0x92` represents the extended scan associated with `e0 12`.

| Reader control | Stock UI ID | SDL/Xorg code | AT scan | Default kernel code | Assigned/retained Linux code |
| --- | ---: | ---: | --- | ---: | ---: |
| Back | 1 | 193 | `0x66` | 0 | 141 |
| Left | 2 | 166 | `0xa6` | 0 | 154 |
| Right | 3 | 163 | `0xa3` | 0 | 160 |
| Up | 4 | 146 | `0x92` | 0 | 152 |
| Down | 5 | 203 | `0x6b` | 0 | 153 |
| Shoot | 8 | 177 | `0xb1` | 0 | 167 |
| Unidentified stock key | 9 | 200 | `0x69` | 0 | 146 |
| OK | 10 | 36 | `0x1c` | 28 | 28 |
| Zoom In | 11 | 151 | `0x97` | 0 | 149 |
| Zoom Out | 12 | 130 | `0x82` or `0x7a` | 0 | 150 |
| Play | 13 | 162 | `0xa2` | 164 | 164 |
| Volume Up | 14 | 176 | `0xb0` | 115 | 115 |
| Volume Down | 15 | 174 | `0xae` | 114 | 114 |
| View Toggle | 16 | 201 | `0x6a` | 0 | 151 |
| Voice Speed | 18 | 147 | `0x93` | 0 | 147 |
| Guide | 19 | 9 | `0x01` | 1 | 1 |
| Favorites | 20 | 159 | `0x9f` | 0 | 148 |
| Options | 25 | 253 | `0xfd` | 0 | 162 |

The inferred translation admitted two candidates for Zoom Out. Neither is used in the corrected map; physical measurements take precedence.

## Power and Hold use additional paths

The stock boot log records a separate ACPI `Power Button (CM)` input device. The POC previously omitted its `button.ko` module. This build retains and loads that module, then lets evdev report the Power key. Power press/release now requests suspend-to-RAM through `antix/power.sh`. Duplicate/wake events are discarded after resume. Physical wake/display recovery still needs a Reader test.

The stock UI's event loop calls `HMGetHoldState` separately. That function reads logical EC entry `0x5a`; the stock `ec` descriptor array resolves it to EC byte `0xa0`, bit 5, width 1 (`KEY_HOLD # State`). The later hardware test also found Hold scan `0x65`; its key value establishes position after a reported transition; initial boot-state reporting remains unchecked. The hardware library also describes UI and motherboard button-state fields. Their presence does not establish a need to poll them for ordinary key input.

`HMInitNonAcpi(1)` runs stock defaults, but `HMInternalSetDefaults` selects access modes 2/3 for writes and skips the button/Hold state entries (access mode 1). We therefore did not copy the stock initializer into the POC or introduce its unrelated EC writes.

## Evidence and reconstruction

All original artifacts remain under ignored `analysis/stock-*`. Only this explanation and the new mapping code are intended for publication.

| Artifact | SHA-256 |
| --- | --- |
| `libcolbert-ui.so` | `1842a19d5a6916cbd707b1fa7f384daead837802b60997d0d8d6b631939ca893` |
| `libcolbert-hm.so` | `d04f176582b5ec6f148a06798778dfa9404161c102be5d59b509fdab2dde8200` |
| `kbd_drv.so` | `29d996c31e3e1eee0b9553518f716de3bb52c5e811e82f0b47976655e58e38ee` |

The reconstruction used exported symbols, `objdump`, actual ELF load-segment mappings, and Hopper through REA. Decompiler output is pseudocode, not recovered original source. Hopper does not expose complete function-body ranges through its public API; jump tables were checked against the original bytes rather than relying solely on pseudocode.

1. UI `EventHandler` constructor at `0x89260`: ordinary, mouse, and raw-scancode maps. Raw constants are initialized at `0x8a086–0x8a250`. REA evidence: `ev_f220ba6950a35c6641c181cd7a303f5a4fdf6bda051bc72a46059820feaafb28`.
2. `SDLKeyToColbertKey` at `0x88a60`: uses the raw map when the SDL keysym is zero; otherwise ordinary symbols or Ctrl shortcuts. Its Ctrl jump table identifies Shoot, Play, Volume, Voice Speed, Favorites, Options, and View Toggle IDs. REA evidence: `ev_248007a5cb770f9408b3d360c7b5f45b75fafcc42ff9091c0423b6966a9a2e2d`.
3. `SDLMouseButtonToColbertKey` at `0x88930`: establishes the separate mouse map, not an extra physical-button driver. REA evidence: `ev_b4c251affb6bb4fc30945323d2d49edc493626e248e2bb85da61265ba97c139a`.
4. Stock Xorg `ATScancode` at `0x32b0`: translates AT bytes into Xorg scancodes before the minimum X keycode offset of 8. Jump tables are at `0x40a8` and `0x4120`. REA evidence: `ev_e2c73c3d617eb4a4ee5e0a24292d9b2bf48696980c8004bde1c5fc8016bb14ca`.
5. The gzip stream inside the stock `vmlinuz` was decompressed into an ELF without execution. `System.map` identifies `atkbd_unxlate_table` at `0xc03c68a0`, `atkbd_set2_keycode` at `0xc03c69a0`, and console raw-emulation `x86_keycodes` at `0xc03bf580`. Values were read through the ELF program headers to establish default assignments and select compatibility key codes.
6. Stock `HMGetHoldState` at `0xa6b0` and `HMInternalSetDefaults` at `0x6490`: REA evidence `ev_e722707901bcef65e77116f059f85b24115e6d8d6ed07d09d458b2eb52c3163f` and `ev_33370ccfdfda2ec42ef339004a2e8f2e71ab43b847f4191ed14f8accfa390552`.
7. The installed user manual's keyboard-shortcut and physical-parts chapters corroborate the control names. Exact text was inspected locally; the copyrighted manual itself is excluded from publication.

Full local REA snapshots are retained in `analysis/inventory/buttons-{ui,hm,xorg}.rea-snapshot.json`. Other derived evidence includes `buttons-constructor.asm`, `buttons-event-loop.asm`, `buttons-xorg-scan.asm`, and `recovered-button-map.json` in that same ignored directory.

The upstream [2.6.27 AT keyboard implementation](https://github.com/torvalds/linux/blob/v2.6.27/drivers/input/keyboard/atkbd.c) explains `MSC_SCAN` reporting for unknown keys and user-loadable keymaps; the [console keyboard implementation](https://github.com/torvalds/linux/blob/v2.6.27/drivers/char/keyboard.c) explains raw emulation. Actual mapping values above come from this Reader's kernel, rather than assuming it matches upstream.

top buttons:
shoot: code 141 scan 0x66
hold: scan 0x65 unknown scan
power: code 116 scan 0xde


on front, top right blue button with white arrow: code 164 scan 0xa2

dpad section:
up and to left of dpad button with diagonal arrow: scan 0x63 unknown scan
dpad up: scan 0x6c unknown scan
dpad left: scan 0x6e unknown scan
enter / dpad center: code 28 scan 0x1c
dpad right: scan 0x6f unknown scan
dpad down: scan 0x6d unknown scan
little menu looking button bottom left of dpad: 0x71 unknown scan

bottom row from left to right:

3 buttons on the left:
minus button: code 151 scan 0x6a
two rectangles button to right of minus button: code 153 scan 0x6b
plus button: code 146 scan 0x69

3 buttons on the right:
"record symbol" looking button: scan 0x68 unknown scan
down arrow (speed?) button: scan 0x67 unknown scan
question mark button: code 1 scan 0x1

right edge:
vol up: code 115 scan 0xb0
vol down: code 114 scan 0xae

Hardware verification (2026-11-07): all displayed physical button labels are correct.
Hold (scan 0x65, assigned code 152): pressed/value 1 = unlocked;
released/value 0 = locked. Initial state at boot has not been checked.

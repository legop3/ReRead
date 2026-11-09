/* Hardware-calibrated on the owner's Reader, 2026-11-07: see buttons.md.
 * AT translated evdev scans, not SDL/Xorg codes. IDs are the app-facing API;
 * Linux keycodes are transport assignments, independent of old stock labels.
 * All labels verified on hardware. Hold value 1 = unlocked, 0 = locked.
 * Initial Hold state at boot has not been verified.
 */
#ifndef READER_KEYS_H
#define READER_KEYS_H
enum reader_button {
    READER_NONE = 0,
    READER_SHOOT, READER_HOLD, READER_POWER, READER_PLAY, READER_BACK,
    READER_UP, READER_DOWN, READER_LEFT, READER_RIGHT, READER_OK,
    READER_OPTIONS, READER_ZOOM_OUT, READER_VIEW_TOGGLE, READER_ZOOM_IN,
    READER_FAVORITES, READER_VOICE_SPEED, READER_GUIDE,
    READER_VOLUME_UP, READER_VOLUME_DOWN
};
struct reader_key { unsigned scan, key; enum reader_button id; const char *name; };
static const struct reader_key reader_keys[] = {
    {0x66,212,READER_SHOOT,"SHOOT"},
    {0x65,152,READER_HOLD,"HOLD"},
    {0xde,116,READER_POWER,"POWER"},
    {0xa2,164,READER_PLAY,"PLAY"},
    {0x63,158,READER_BACK,"BACK"},
    {0x6c,103,READER_UP,"UP"},
    {0x6d,108,READER_DOWN,"DOWN"},
    {0x6e,105,READER_LEFT,"LEFT"},
    {0x6f,106,READER_RIGHT,"RIGHT"},
    {0x1c,28,READER_OK,"OK"},
    {0x71,139,READER_OPTIONS,"OPTIONS"},
    {0x6a,419,READER_ZOOM_OUT,"ZOOM OUT"},
    {0x6b,227,READER_VIEW_TOGGLE,"VIEW TOGGLE"},
    {0x69,418,READER_ZOOM_IN,"ZOOM IN"},
    {0x68,156,READER_FAVORITES,"FAVORITES"},
    {0x67,208,READER_VOICE_SPEED,"VOICE SPEED"},
    {0x01,138,READER_GUIDE,"GUIDE"},
    {0xb0,115,READER_VOLUME_UP,"VOLUME UP"},
    {0xae,114,READER_VOLUME_DOWN,"VOLUME DOWN"}
};
#define READER_KEY_COUNT (sizeof(reader_keys)/sizeof(reader_keys[0]))
#endif

/*
    translate.h - PS5Instrumentalizer
    Pure (libc-free) conversion from Xbox 360 XInput guitar reports to the
    report format of the PS3 Rock Band guitar (12BA:0200) that Rock Band 4
    expects. Used by the in-game stub and by the host unit tests.

    Licensed under the GNU Lesser General Public License version 2.1, or later.
*/

#ifndef PS5I_TRANSLATE_H
#define PS5I_TRANSLATE_H

#include <stdint.h>
#include "xinput.h"

#define BIT(i) (1u << (i))

/* PS3 guitar button bits (ps3_rb_guitar_report::buttons) */
#define PS3_GTR_BLUE    BIT(0)
#define PS3_GTR_GREEN   BIT(1)
#define PS3_GTR_RED     BIT(2)
#define PS3_GTR_YELLOW  BIT(3)
#define PS3_GTR_ORANGE  BIT(4)
#define PS3_GTR_BACK    BIT(8)
#define PS3_GTR_START   BIT(9)

/* PS3 hat values (strum bar / d-pad) */
#define PS3_HAT_UP      0x00
#define PS3_HAT_RIGHT   0x02
#define PS3_HAT_DOWN    0x04
#define PS3_HAT_LEFT    0x06
#define PS3_HAT_NEUTRAL 0x08

/*
    Same byte layout as the original plugin's struct (which relied on natural
    alignment); the compiler padding byte at offset 21 is spelled out here so
    the layout is explicit and independent of packing rules.
*/
typedef struct ps3_rb_guitar_report {
    uint16_t buttons;      /* 0  */
    uint8_t hat;           /* 2  */
    uint8_t left_joy_x;    /* 3  */
    uint8_t left_joy_y;    /* 4  */
    uint8_t whammy;        /* 5  */
    uint8_t mode_switch;   /* 6  */
    uint8_t padding[12];   /* 7  */
    uint8_t accel_x;       /* 19 (tilt) */
    uint8_t unk_1;         /* 20 */
    uint8_t pad_21;        /* 21 */
    uint16_t accel_z;      /* 22 */
    uint8_t accel_y;       /* 24 */
    uint8_t unk_2;         /* 25 */
    uint16_t gyro;         /* 26 */
} ps3_rb_guitar_report;

#define PS3_GUITAR_REPORT_SIZE 28

/*
    Converts an XInput axis (int16, centre 0) to an unsigned byte
    (-32768 -> 0x00, 0 -> 0x80, 32767 -> 0xFF).
*/
uint8_t translate_axis_to_u8(int16_t value);

/*
    Axis conversion used by the original OrbisInstrumentalizer
    (raw reinterpretation of the int16 as uint16, high byte). Kept behind a
    config flag in case the corrected mapping misbehaves on some guitars.
*/
uint8_t translate_axis_legacy(int16_t value);

/*
    Translates an XInput "controller state" report into a PS3 guitar report.
    `flags` is the STUB_FLAG_* bitmask from stub_abi.h.
*/
void translate_xinput_guitar(const xinput_report_controls *in, uint32_t flags, ps3_rb_guitar_report *out);

/*
    Writes a neutral (nothing pressed) PS3 guitar report.
*/
void translate_neutral_guitar(ps3_rb_guitar_report *out);

#endif

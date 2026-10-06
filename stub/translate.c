/*
    translate.c - PS5Instrumentalizer
    XInput guitar -> PS3 Rock Band guitar report conversion.
    Based on the mapping from OrbisInstrumentalizer by InvoxiPlayGames.
    Licensed under the GNU Lesser General Public License version 2.1, or later.

    This file must stay free of libc calls: it is compiled both into the
    freestanding in-game stub and into the host unit tests.
*/

#include "translate.h"
#include "stub_abi.h"

uint8_t translate_axis_to_u8(int16_t value) {
    return (uint8_t)(((int32_t)value + 32768) >> 8);
}

uint8_t translate_axis_legacy(int16_t value) {
    return (uint8_t)((uint16_t)value / 0x100);
}

void translate_neutral_guitar(ps3_rb_guitar_report *out) {
    uint8_t *p = (uint8_t *)out;
    for (unsigned i = 0; i < sizeof(*out); i++)
        p[i] = 0;
    out->hat = PS3_HAT_NEUTRAL;
}

void translate_xinput_guitar(const xinput_report_controls *in, uint32_t flags, ps3_rb_guitar_report *out) {
    uint16_t buttons = 0;

    translate_neutral_guitar(out);

    /* fret buttons */
    if (in->buttons2 & XINPUT_BUTTON_A) buttons |= PS3_GTR_GREEN;
    if (in->buttons2 & XINPUT_BUTTON_B) buttons |= PS3_GTR_RED;
    if (in->buttons2 & XINPUT_BUTTON_Y) buttons |= PS3_GTR_YELLOW;
    if (in->buttons2 & XINPUT_BUTTON_X) buttons |= PS3_GTR_BLUE;
    if (in->buttons2 & XINPUT_BUTTON_LB) buttons |= PS3_GTR_ORANGE;

    /* special buttons */
    if (in->buttons1 & XINPUT_BUTTON_BACK) buttons |= PS3_GTR_BACK;
    if (in->buttons1 & XINPUT_BUTTON_START) buttons |= PS3_GTR_START;
    out->buttons = buttons;

    /* d-pad / strum bar (same priority order as the original plugin) */
    if (in->buttons1 & XINPUT_BUTTON_UP) out->hat = PS3_HAT_UP;
    if (in->buttons1 & XINPUT_BUTTON_DOWN) out->hat = PS3_HAT_DOWN;
    if (in->buttons1 & XINPUT_BUTTON_LEFT) out->hat = PS3_HAT_LEFT;
    if (in->buttons1 & XINPUT_BUTTON_RIGHT) out->hat = PS3_HAT_RIGHT;

    /* whammy bar (right stick X) and tilt (right stick Y) */
    if (flags & STUB_FLAG_WHAMMY_LEGACY)
        out->whammy = translate_axis_legacy(in->right_stick_x);
    else
        out->whammy = translate_axis_to_u8(in->right_stick_x);

    if (flags & STUB_FLAG_TILT_LEGACY)
        out->accel_x = translate_axis_legacy(in->right_stick_y);
    else
        out->accel_x = translate_axis_to_u8(in->right_stick_y);
}

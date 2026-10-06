/*
    test_translate.c - PS5Instrumentalizer
    Host-side unit tests for the XInput -> PS3 guitar report translation.
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "translate.h"
#include "stub_abi.h"

static int failures = 0;
static int checks = 0;

#define CHECK_EQ(actual, expected, what)                                              \
    do {                                                                              \
        checks++;                                                                     \
        long a_ = (long)(actual), e_ = (long)(expected);                              \
        if (a_ != e_) {                                                               \
            failures++;                                                               \
            printf("FAIL %s:%d %s: got 0x%lx expected 0x%lx\n", __FILE__, __LINE__,   \
                   what, a_, e_);                                                     \
        }                                                                             \
    } while (0)

static xinput_report_controls rest_report(void) {
    xinput_report_controls r;
    memset(&r, 0, sizeof(r));
    r.header.message_type = 0x00;
    r.header.message_size = 0x14;
    r.right_stick_x = -32768; /* whammy at rest */
    return r;
}

static void test_layout(void) {
    CHECK_EQ(sizeof(ps3_rb_guitar_report), PS3_GUITAR_REPORT_SIZE, "ps3 report size");
    CHECK_EQ(sizeof(xinput_report_controls), 20, "xinput report size");
    CHECK_EQ(__builtin_offsetof(ps3_rb_guitar_report, hat), 2, "hat offset");
    CHECK_EQ(__builtin_offsetof(ps3_rb_guitar_report, whammy), 5, "whammy offset");
    CHECK_EQ(__builtin_offsetof(ps3_rb_guitar_report, accel_x), 19, "accel_x offset");
    CHECK_EQ(__builtin_offsetof(ps3_rb_guitar_report, accel_z), 22, "accel_z offset");
    CHECK_EQ(__builtin_offsetof(ps3_rb_guitar_report, gyro), 26, "gyro offset");
}

static void test_axis(void) {
    CHECK_EQ(translate_axis_to_u8(-32768), 0x00, "axis min");
    CHECK_EQ(translate_axis_to_u8(0), 0x80, "axis centre");
    CHECK_EQ(translate_axis_to_u8(32767), 0xFF, "axis max");
    CHECK_EQ(translate_axis_to_u8(-1), 0x7F, "axis -1");

    /* legacy mapping: raw reinterpretation of the int16 */
    CHECK_EQ(translate_axis_legacy(0), 0x00, "legacy zero");
    CHECK_EQ(translate_axis_legacy(32767), 0x7F, "legacy max");
    CHECK_EQ(translate_axis_legacy(-32768), 0x80, "legacy min");
}

static void test_neutral(void) {
    ps3_rb_guitar_report out;
    xinput_report_controls in = rest_report();
    translate_xinput_guitar(&in, 0, &out);
    CHECK_EQ(out.buttons, 0, "neutral buttons");
    CHECK_EQ(out.hat, PS3_HAT_NEUTRAL, "neutral hat");
    CHECK_EQ(out.whammy, 0x00, "whammy at rest");
    CHECK_EQ(out.accel_x, 0x80, "tilt at centre");

    translate_neutral_guitar(&out);
    CHECK_EQ(out.hat, PS3_HAT_NEUTRAL, "neutral helper hat");
    CHECK_EQ(out.buttons, 0, "neutral helper buttons");
}

static void test_frets(void) {
    static const struct { uint8_t mask; uint16_t expect; const char *name; } frets[] = {
        { XINPUT_BUTTON_A,  PS3_GTR_GREEN,  "green"  },
        { XINPUT_BUTTON_B,  PS3_GTR_RED,    "red"    },
        { XINPUT_BUTTON_Y,  PS3_GTR_YELLOW, "yellow" },
        { XINPUT_BUTTON_X,  PS3_GTR_BLUE,   "blue"   },
        { XINPUT_BUTTON_LB, PS3_GTR_ORANGE, "orange" },
    };
    for (unsigned i = 0; i < sizeof(frets) / sizeof(frets[0]); i++) {
        ps3_rb_guitar_report out;
        xinput_report_controls in = rest_report();
        in.buttons2 = frets[i].mask;
        translate_xinput_guitar(&in, 0, &out);
        CHECK_EQ(out.buttons, frets[i].expect, frets[i].name);
    }

    /* chord: green + red + yellow */
    {
        ps3_rb_guitar_report out;
        xinput_report_controls in = rest_report();
        in.buttons2 = XINPUT_BUTTON_A | XINPUT_BUTTON_B | XINPUT_BUTTON_Y;
        translate_xinput_guitar(&in, 0, &out);
        CHECK_EQ(out.buttons, PS3_GTR_GREEN | PS3_GTR_RED | PS3_GTR_YELLOW, "chord");
    }
}

static void test_strum_and_menu(void) {
    ps3_rb_guitar_report out;
    xinput_report_controls in;

    in = rest_report(); in.buttons1 = XINPUT_BUTTON_UP;
    translate_xinput_guitar(&in, 0, &out);
    CHECK_EQ(out.hat, PS3_HAT_UP, "strum up");

    in = rest_report(); in.buttons1 = XINPUT_BUTTON_DOWN;
    translate_xinput_guitar(&in, 0, &out);
    CHECK_EQ(out.hat, PS3_HAT_DOWN, "strum down");

    in = rest_report(); in.buttons1 = XINPUT_BUTTON_LEFT;
    translate_xinput_guitar(&in, 0, &out);
    CHECK_EQ(out.hat, PS3_HAT_LEFT, "dpad left");

    in = rest_report(); in.buttons1 = XINPUT_BUTTON_RIGHT;
    translate_xinput_guitar(&in, 0, &out);
    CHECK_EQ(out.hat, PS3_HAT_RIGHT, "dpad right");

    in = rest_report(); in.buttons1 = XINPUT_BUTTON_START | XINPUT_BUTTON_BACK;
    translate_xinput_guitar(&in, 0, &out);
    CHECK_EQ(out.buttons, PS3_GTR_START | PS3_GTR_BACK, "start+back");
    CHECK_EQ(out.hat, PS3_HAT_NEUTRAL, "hat unaffected by start/back");
}

static void test_whammy_tilt(void) {
    ps3_rb_guitar_report out;
    xinput_report_controls in = rest_report();

    in.right_stick_x = 32767;
    in.right_stick_y = 32767;
    translate_xinput_guitar(&in, 0, &out);
    CHECK_EQ(out.whammy, 0xFF, "whammy full");
    CHECK_EQ(out.accel_x, 0xFF, "tilt full");

    in.right_stick_x = 0;
    in.right_stick_y = -32768;
    translate_xinput_guitar(&in, 0, &out);
    CHECK_EQ(out.whammy, 0x80, "whammy half");
    CHECK_EQ(out.accel_x, 0x00, "tilt min");

    /* legacy flags restore the original mapping */
    in.right_stick_x = 32767;
    in.right_stick_y = 32767;
    translate_xinput_guitar(&in, STUB_FLAG_WHAMMY_LEGACY | STUB_FLAG_TILT_LEGACY, &out);
    CHECK_EQ(out.whammy, 0x7F, "legacy whammy");
    CHECK_EQ(out.accel_x, 0x7F, "legacy tilt");
}

int main(void) {
    test_layout();
    test_axis();
    test_neutral();
    test_frets();
    test_strum_and_menu();
    test_whammy_tilt();

    printf("test_translate: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

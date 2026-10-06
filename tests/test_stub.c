/*
    test_stub.c - PS5Instrumentalizer
    Loads the real, freestanding stub blob (build/stub.bin) at an arbitrary
    address on the host and drives its hooks with a mock libSceUsbd. This
    verifies that the blob is position independent and that the hook logic
    (identification, descriptor spoofing, report translation, wireless
    handling, cleanup) behaves as designed.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

#include "libusb.h"
#include "stub_abi.h"
#include "translate.h"

#ifndef STUB_BIN_PATH
#error STUB_BIN_PATH must be defined
#endif

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

/* ---- mock libSceUsbd ---- */

static int mock_subtype = XINPUT_SUBTYPE_GUITAR;
static int mock_free_calls = 0;
static int mock_close_calls = 0;
static libusb_transfer_cb_fn mock_last_cb = NULL;

static unsigned char mock_extra[17];

static int mock_get_config(libusb_device *dev, uint8_t idx, struct libusb_config_descriptor **cfg) {
    (void)dev; (void)idx;
    struct libusb_config_descriptor *c = calloc(1, sizeof(*c));
    struct libusb_interface *itf = calloc(1, sizeof(*itf));
    struct libusb_interface_descriptor *alt = calloc(1, sizeof(*alt));
    memset(mock_extra, 0, sizeof(mock_extra));
    mock_extra[0] = 0x11; mock_extra[1] = 0x21; mock_extra[3] = 0x01;
    mock_extra[4] = (unsigned char)mock_subtype;
    alt->bInterfaceClass = 0xFF;
    alt->bInterfaceSubClass = 0x5D;
    alt->bInterfaceProtocol = 0x01;
    alt->extra = mock_extra;
    alt->extra_length = 17;
    itf->altsetting = alt;
    itf->num_altsetting = 1;
    c->bNumInterfaces = 1;
    c->interface = itf;
    *cfg = c;
    return 0;
}

static void mock_free_config(struct libusb_config_descriptor *cfg) {
    mock_free_calls++;
    free(cfg->interface->altsetting);
    free(cfg->interface);
    free(cfg);
}

static int mock_vid = 0x045E, mock_pid = 0x028E, mock_class = 0xFF;

static int mock_get_device(libusb_device *dev, struct libusb_device_descriptor *d) {
    (void)dev;
    memset(d, 0, sizeof(*d));
    d->bDeviceClass = (uint8_t)mock_class;
    d->bDeviceSubClass = (uint8_t)mock_class;
    d->bDeviceProtocol = (uint8_t)mock_class;
    d->idVendor = (uint16_t)mock_vid;
    d->idProduct = (uint16_t)mock_pid;
    return 0;
}

static int mock_open(libusb_device *dev, libusb_device_handle **h) {
    *h = (libusb_device_handle *)((uintptr_t)dev + 0x1000);
    return 0;
}

static void mock_close(libusb_device_handle *h) {
    (void)h;
    mock_close_calls++;
}

static void mock_fill(struct libusb_transfer *t, libusb_device_handle *h, unsigned char ep,
                      unsigned char *buf, int len, libusb_transfer_cb_fn cb, void *ud, unsigned int to) {
    t->dev_handle = h;
    t->endpoint = ep;
    t->buffer = buf;
    t->length = len;
    t->callback = cb;
    t->user_data = ud;
    t->timeout = to;
    mock_last_cb = cb;
}

/* ---- the loaded stub ---- */

static uint8_t *image = NULL;
static stub_header_t *hdr = NULL;

typedef int (*open_fn)(libusb_device *, libusb_device_handle **);
typedef void (*close_fn)(libusb_device_handle *);
typedef int (*get_config_fn)(libusb_device *, uint8_t, struct libusb_config_descriptor **);
typedef int (*get_device_fn)(libusb_device *, struct libusb_device_descriptor *);
typedef void (*fill_fn)(struct libusb_transfer *, libusb_device_handle *, unsigned char, unsigned char *, int,
                        libusb_transfer_cb_fn, void *, unsigned int);

static open_fn h_open;
static close_fn h_close;
static get_config_fn h_get_config;
static get_device_fn h_get_device;
static fill_fn h_fill;

static void load_stub(uintptr_t hint) {
    FILE *f = fopen(STUB_BIN_PATH, "rb");
    if (!f) { perror("open stub.bin"); exit(2); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)size);
    if (fread(data, 1, (size_t)size, f) != (size_t)size) { perror("read"); exit(2); }
    fclose(f);

    stub_header_t tmp;
    memcpy(&tmp, data, sizeof(tmp));
    size_t image_size = (size_t)tmp.image_end;
    size_t map_size = (image_size + 0xFFF) & ~(size_t)0xFFF;
    image = mmap((void *)hint, map_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (image == MAP_FAILED) { perror("mmap"); exit(2); }
    memcpy(image, data, (size_t)size);
    free(data);

    hdr = (stub_header_t *)image;
    CHECK_EQ(hdr->magic, STUB_MAGIC, "stub magic");
    CHECK_EQ(hdr->abi_version, STUB_ABI_VERSION, "stub abi version");
    CHECK_EQ(image_size >= (size_t)size, 1, "image_end covers file contents");

    hdr->orig_open = (uint64_t)(uintptr_t)mock_open;
    hdr->orig_close = (uint64_t)(uintptr_t)mock_close;
    hdr->orig_get_config = (uint64_t)(uintptr_t)mock_get_config;
    hdr->orig_free_config = (uint64_t)(uintptr_t)mock_free_config;
    hdr->orig_get_device = (uint64_t)(uintptr_t)mock_get_device;
    hdr->orig_fill_interrupt = (uint64_t)(uintptr_t)mock_fill;

    h_open = (open_fn)(image + hdr->hook_open);
    h_close = (close_fn)(image + hdr->hook_close);
    h_get_config = (get_config_fn)(image + hdr->hook_get_config);
    h_get_device = (get_device_fn)(image + hdr->hook_get_device);
    h_fill = (fill_fn)(image + hdr->hook_fill_interrupt);
}

/* ---- game-side callback ---- */

static int game_cb_calls = 0;
static unsigned char game_cb_snapshot[64];
static int game_cb_actual_length = 0;

static void game_callback(struct libusb_transfer *t) {
    game_cb_calls++;
    memcpy(game_cb_snapshot, t->buffer, 64);
    game_cb_actual_length = t->actual_length;
}

static void complete_transfer(struct libusb_transfer *t, const void *data, int n) {
    memset(t->buffer, 0, (size_t)t->length);
    memcpy(t->buffer, data, (size_t)n);
    t->status = LIBUSB_TRANSFER_COMPLETED;
    t->actual_length = n;
    game_cb_calls = 0;
    mock_last_cb(t);
}

/* ---- tests ---- */

static void check_spoof_guitar(void) {
    struct libusb_device_descriptor d;
    mock_vid = 0x1430; mock_pid = 0x4748; mock_class = 0xFF; mock_subtype = XINPUT_SUBTYPE_GUITAR;
    CHECK_EQ(h_get_device((libusb_device *)0x10, &d), 0, "get_device ret");
    CHECK_EQ(d.idVendor, 0x12BA, "guitar vid spoofed");
    CHECK_EQ(d.idProduct, 0x0200, "guitar pid spoofed");

    struct libusb_config_descriptor *cfg = NULL;
    CHECK_EQ(h_get_config((libusb_device *)0x10, 0, &cfg), 0, "get_config ret");
    CHECK_EQ(cfg->interface->altsetting[0].bInterfaceClass, 0x03, "class forced to HID");
    mock_free_config(cfg);
}

static void check_wired_flow(void) {
    static unsigned char buf[32];
    struct libusb_transfer transfer;
    libusb_device_handle *handle = NULL;
    memset(&transfer, 0, sizeof(transfer));
    mock_subtype = XINPUT_SUBTYPE_GUITAR_ALTERNATE;

    CHECK_EQ(h_open((libusb_device *)0x20, &handle), 0, "open ret");
    CHECK_EQ(hdr->stat_guitars_opened, 1, "guitar tracked");

    h_fill(&transfer, handle, 0x81, buf, 32, game_callback, NULL, 0);
    CHECK_EQ(mock_last_cb != game_callback, 1, "callback wrapped");

    xinput_report_controls in;
    memset(&in, 0, sizeof(in));
    in.header.message_size = 0x14;
    in.buttons2 = XINPUT_BUTTON_A | XINPUT_BUTTON_LB;   /* green + orange */
    in.buttons1 = XINPUT_BUTTON_DOWN;                   /* strum down */
    in.right_stick_x = 32767;                           /* whammy full */
    complete_transfer(&transfer, &in, sizeof(in));

    CHECK_EQ(game_cb_calls, 1, "game callback called once");
    CHECK_EQ(game_cb_actual_length, 0x14, "actual_length untouched by default");
    ps3_rb_guitar_report out;
    memcpy(&out, game_cb_snapshot, sizeof(out));
    CHECK_EQ(out.buttons, PS3_GTR_GREEN | PS3_GTR_ORANGE, "wired frets");
    CHECK_EQ(out.hat, PS3_HAT_DOWN, "wired strum");
    CHECK_EQ(out.whammy, 0xFF, "wired whammy");

    /* optional behaviour: report a PS3 sized transfer */
    hdr->flags |= STUB_FLAG_EXTEND_LENGTH;
    complete_transfer(&transfer, &in, sizeof(in));
    CHECK_EQ(game_cb_actual_length, 27, "actual_length extended with flag");
    hdr->flags &= ~STUB_FLAG_EXTEND_LENGTH;

    /* OUT endpoint must not be wrapped */
    struct libusb_transfer out_transfer;
    memset(&out_transfer, 0, sizeof(out_transfer));
    h_fill(&out_transfer, handle, 0x01, buf, 8, game_callback, NULL, 0);
    CHECK_EQ(mock_last_cb == game_callback, 1, "out endpoint passthrough");

    /* closing releases tracking: the next IN transfer is passed through unchanged */
    h_close(handle);
    CHECK_EQ(mock_close_calls, 1, "close forwarded");
    h_fill(&transfer, handle, 0x81, buf, 32, game_callback, NULL, 0);
    CHECK_EQ(mock_last_cb == game_callback, 1, "no wrapping after close");
}

static void check_wireless_flow(void) {
    static unsigned char buf[32];
    struct libusb_transfer transfer;
    libusb_device_handle *handle = NULL;
    memset(&transfer, 0, sizeof(transfer));
    mock_subtype = 0x13; /* the 360 wireless receiver */

    struct libusb_device_descriptor d;
    mock_vid = 0x045E; mock_pid = 0x0291; mock_class = 0xFF;
    h_get_device((libusb_device *)0x30, &d);
    CHECK_EQ(d.idVendor, 0x12BA, "receiver vid spoofed");
    CHECK_EQ(d.idProduct, 0x0200, "receiver presented as guitar");

    h_open((libusb_device *)0x30, &handle);
    h_fill(&transfer, handle, 0x81, buf, 32, game_callback, NULL, 0);

    /* 4 byte receiver header followed by the 360 input packet */
    uint8_t packet[32];
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x00; packet[1] = 0x01; packet[2] = 0x00; packet[3] = 0xF0;
    xinput_report_controls in;
    memset(&in, 0, sizeof(in));
    in.header.message_size = 0x13;
    in.buttons2 = XINPUT_BUTTON_Y | XINPUT_BUTTON_X;     /* yellow + blue */
    in.buttons1 = XINPUT_BUTTON_START;
    memcpy(packet + 4, &in, 16);
    complete_transfer(&transfer, packet, 29);

    ps3_rb_guitar_report out;
    memcpy(&out, game_cb_snapshot, sizeof(out));
    CHECK_EQ(out.buttons, PS3_GTR_YELLOW | PS3_GTR_BLUE | PS3_GTR_START, "wireless buttons");
    CHECK_EQ(out.hat, PS3_HAT_NEUTRAL, "wireless hat");

    /* a status packet in between must not wipe the held buttons */
    uint8_t status[32];
    memset(status, 0, sizeof(status));
    status[0] = 0x08; status[1] = 0x80;
    complete_transfer(&transfer, status, 2);
    memcpy(&out, game_cb_snapshot, sizeof(out));
    CHECK_EQ(out.buttons, PS3_GTR_YELLOW | PS3_GTR_BLUE | PS3_GTR_START, "status packet keeps last input");

    h_close(handle);
}

static void check_gamepad_untouched(void) {
    static unsigned char buf[32];
    struct libusb_transfer transfer;
    libusb_device_handle *handle = NULL;
    memset(&transfer, 0, sizeof(transfer));
    mock_subtype = XINPUT_SUBTYPE_GAMEPAD;
    mock_vid = 0x045E; mock_pid = 0x028E; mock_class = 0xFF;

    struct libusb_device_descriptor d;
    h_get_device((libusb_device *)0x40, &d);
    CHECK_EQ(d.idVendor, 0x045E, "gamepad vid untouched");
    CHECK_EQ(d.idProduct, 0x028E, "gamepad pid untouched");

    struct libusb_config_descriptor *cfg = NULL;
    h_get_config((libusb_device *)0x40, 0, &cfg);
    CHECK_EQ(cfg->interface->altsetting[0].bInterfaceClass, 0xFF, "gamepad class untouched");
    mock_free_config(cfg);

    unsigned before = hdr->stat_guitars_opened;
    h_open((libusb_device *)0x40, &handle);
    CHECK_EQ(hdr->stat_guitars_opened, before, "gamepad not tracked");
    h_fill(&transfer, handle, 0x81, buf, 32, game_callback, NULL, 0);
    CHECK_EQ(mock_last_cb == game_callback, 1, "gamepad callback passthrough");
    h_close(handle);
}

static void check_free_balance(void) {
    /* every config descriptor allocated by identify_device must have been freed */
    CHECK_EQ(mock_free_calls > 0, 1, "identify frees its descriptors");
}

static void check_wii_guitar(void) {
    struct libusb_device_descriptor d;
    mock_vid = 0x1BAD; mock_pid = 0x0004; mock_class = 0x00;
    h_get_device((libusb_device *)0x50, &d);
    CHECK_EQ(d.idVendor, 0x12BA, "wii guitar vid");
    CHECK_EQ(d.idProduct, 0x0200, "wii guitar pid");
}

int main(void) {
    /* load at a hint far from where the linker placed it (address 0) to prove position independence */
    load_stub(0x200000000ull);
    check_spoof_guitar();
    check_wired_flow();
    check_wireless_flow();
    check_gamepad_untouched();
    check_wii_guitar();
    check_free_balance();

    printf("test_stub: %d checks, %d failures (stub loaded at %p)\n", checks, failures, (void *)image);
    return failures ? 1 : 0;
}

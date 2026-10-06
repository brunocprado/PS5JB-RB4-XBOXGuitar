/*
    usb_hooks.c - PS5Instrumentalizer
    Hooks for the libSceUsbd imports of Rock Band 4. This code is injected
    into the game process (PS4 ABI, x86-64) as a freestanding,
    position-independent blob; it has no libc and no relocations. The real
    libSceUsbd functions are reached through the pointer table in
    stub_header_t, filled in by the injector.

    Ported from OrbisInstrumentalizer (usbd_hooks_rb4.c) by InvoxiPlayGames.
    Licensed under the GNU Lesser General Public License version 2.1, or later.
*/

#include <stdint.h>
#include <stddef.h>

#include "libusb.h"
#include "xinput.h"
#include "stub_abi.h"
#include "translate.h"

/* Product IDs that Rock Band 4 recognises (licensed by SCEA). */
#define RB4_VENDOR_ID         0x12BA
#define RB4_PRODUCT_GUITAR    0x0200

/* Harmonix vendor ID used by Wii instruments (HID, already PS3-like). */
#define HARMONIX_VENDOR_ID    0x1BAD
#define WII_PRODUCT_GUITAR    0x0004

#define XINPUT_INTERFACE_SUBCLASS 0x5D
#define USB_CLASS_HID             0x03
#define USB_ENDPOINT_IN           0x80

#define MAX_DEVICES   4
#define MAX_TRANSFERS 16
#define WIRELESS_REPORT_MAX 32

typedef int  (*fn_open_t)(libusb_device *, libusb_device_handle **);
typedef void (*fn_close_t)(libusb_device_handle *);
typedef int  (*fn_get_config_t)(libusb_device *, uint8_t, struct libusb_config_descriptor **);
typedef void (*fn_free_config_t)(struct libusb_config_descriptor *);
typedef int  (*fn_get_device_t)(libusb_device *, struct libusb_device_descriptor *);
typedef void (*fn_fill_interrupt_t)(struct libusb_transfer *, libusb_device_handle *, unsigned char,
                                    unsigned char *, int, libusb_transfer_cb_fn, void *, unsigned int);

typedef enum {
    DEV_NONE = 0,
    DEV_XINPUT_GUITAR,   /* wired 360 guitar (RB or GH) */
    DEV_XINPUT_WIRELESS  /* 360 wireless receiver, instrument behind it */
} device_type_t;

typedef struct {
    libusb_device_handle *handle;   /* NULL = free slot */
    device_type_t type;
    uint8_t last_report[WIRELESS_REPORT_MAX];
} open_device_t;

typedef struct {
    struct libusb_transfer *transfer; /* NULL = free slot */
    libusb_device_handle *handle;
    libusb_transfer_cb_fn orig_cb;
} tracked_transfer_t;

/* ---- image header: must be the first thing in the image ---- */

extern char __stub_end[]; /* provided by stub.ld, end of .bss */

#define HDR_SECTION __attribute__((section(".header"), used, aligned(16)))

int stub_hook_open(libusb_device *device, libusb_device_handle **out_handle);
void stub_hook_close(libusb_device_handle *handle);
int stub_hook_get_config(libusb_device *device, uint8_t index, struct libusb_config_descriptor **config);
int stub_hook_get_device(libusb_device *device, struct libusb_device_descriptor *desc);
void stub_hook_fill_interrupt(struct libusb_transfer *transfer, libusb_device_handle *handle,
                              unsigned char endpoint, unsigned char *buffer, int length,
                              libusb_transfer_cb_fn callback, void *user_data, unsigned int timeout);

HDR_SECTION stub_header_t g_stub_header = {
    .magic = STUB_MAGIC,
    .abi_version = STUB_ABI_VERSION,
    .flags = 0,
    /* the image is linked at address 0, so these addresses are offsets from the image base */
    .image_end = (uint64_t)(uintptr_t)__stub_end,
    .hook_open = (uint64_t)(uintptr_t)stub_hook_open,
    .hook_close = (uint64_t)(uintptr_t)stub_hook_close,
    .hook_get_config = (uint64_t)(uintptr_t)stub_hook_get_config,
    .hook_get_device = (uint64_t)(uintptr_t)stub_hook_get_device,
    .hook_fill_interrupt = (uint64_t)(uintptr_t)stub_hook_fill_interrupt,
};

/* ---- state ---- */

static open_device_t g_devices[MAX_DEVICES];
static tracked_transfer_t g_transfers[MAX_TRANSFERS];
static volatile int g_lock;

#define HDR (&g_stub_header)

/* ---- tiny libc replacements (the compiler may emit calls to these) ---- */

void *memcpy(void *dst, const void *src, size_t n) {
    void *ret = dst;
    __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

void *memset(void *dst, int c, size_t n) {
    void *ret = dst;
    __asm__ volatile("rep stosb" : "+D"(dst), "+c"(n) : "a"(c) : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        for (size_t i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
    return dst;
}

/* ---- locking ---- */

static inline void lock_acquire(void) {
    while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}

static inline void lock_release(void) {
    __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE);
}

static inline void stat_inc(volatile uint32_t *counter) {
    __atomic_fetch_add(counter, 1, __ATOMIC_RELAXED);
}

static void trace(uint32_t code, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t index = __atomic_fetch_add(&HDR->trace_head, 1, __ATOMIC_RELAXED) % STUB_TRACE_SLOTS;
    stub_trace_event_t *e = &HDR->trace[index];
    e->a = a;
    e->b = b;
    e->c = c;
    __atomic_store_n(&e->code, code, __ATOMIC_RELEASE);
}

/* ---- original function accessors ---- */

#define ORIG(type, field) ((type)(uintptr_t)HDR->field)

/* ---- device table (callers must hold the lock) ---- */

static open_device_t *find_device_locked(libusb_device_handle *handle) {
    if (handle == NULL)
        return NULL;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_devices[i].handle == handle)
            return &g_devices[i];
    }
    return NULL;
}

static open_device_t *alloc_device_locked(libusb_device_handle *handle) {
    open_device_t *existing = find_device_locked(handle);
    if (existing != NULL)
        return existing;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_devices[i].handle == NULL) {
            memset(&g_devices[i], 0, sizeof(g_devices[i]));
            g_devices[i].handle = handle;
            return &g_devices[i];
        }
    }
    return NULL;
}

static tracked_transfer_t *find_transfer_locked(struct libusb_transfer *transfer) {
    for (int i = 0; i < MAX_TRANSFERS; i++) {
        if (g_transfers[i].transfer == transfer)
            return &g_transfers[i];
    }
    return NULL;
}

static tracked_transfer_t *alloc_transfer_locked(struct libusb_transfer *transfer) {
    tracked_transfer_t *existing = find_transfer_locked(transfer);
    if (existing != NULL)
        return existing;
    for (int i = 0; i < MAX_TRANSFERS; i++) {
        if (g_transfers[i].transfer == NULL)
            return &g_transfers[i];
    }
    return NULL;
}

/* ---- device identification ---- */

static device_type_t classify_config(const struct libusb_config_descriptor *config) {
    if (config == NULL || config->interface == NULL || config->bNumInterfaces < 1)
        return DEV_NONE;
    const struct libusb_interface_descriptor *alt = config->interface->altsetting;
    if (alt == NULL || alt->bInterfaceSubClass != XINPUT_INTERFACE_SUBCLASS)
        return DEV_NONE;
    if (alt->extra == NULL || alt->extra_length <= 6)
        return DEV_NONE;

    /* the controller subtype is at the 5th byte of the class-specific descriptor */
    uint8_t subtype = alt->extra[4];
    switch (subtype) {
        case XINPUT_SUBTYPE_GUITAR:
        case XINPUT_SUBTYPE_GUITAR_ALTERNATE:
        case XINPUT_SUBTYPE_GUITAR_BASS:
            return DEV_XINPUT_GUITAR;
        case XINPUT_SUBTYPE_GAMEPAD:
        case XINPUT_SUBTYPE_WHEEL:
        case XINPUT_SUBTYPE_ARCADE_STICK:
        case XINPUT_SUBTYPE_FLIGHT_STICK:
        case XINPUT_SUBTYPE_DANCEPAD:
        case XINPUT_SUBTYPE_DRUM_KIT:
            /* wired XInput devices that are not guitars: leave them alone */
            return DEV_NONE;
        default:
            /* anything else (e.g. 0x13 on the wireless receiver) is assumed to be the 360 wireless receiver */
            return DEV_XINPUT_WIRELESS;
    }
}

static device_type_t identify_device(libusb_device *device) {
    struct libusb_config_descriptor *config = NULL;
    device_type_t type = DEV_NONE;
    int r = ORIG(fn_get_config_t, orig_get_config)(device, 0, &config);
    if (r == 0 && config != NULL) {
        type = classify_config(config);
        ORIG(fn_free_config_t, orig_free_config)(config);
    }
    return type;
}

/* ---- report translation callbacks ---- */

static void finish_transfer(struct libusb_transfer *transfer, const ps3_rb_guitar_report *report) {
    int length = transfer->length;
    if (length > PS3_GUITAR_REPORT_SIZE)
        length = PS3_GUITAR_REPORT_SIZE;
    if (length <= 0 || transfer->buffer == NULL)
        return;
    memcpy(transfer->buffer, report, (size_t)length);
    /*
        Optionally make the game see a full-sized report, like it would from a
        PS3 guitar (27 bytes). Off by default: the PS4 plugin never touched it.
    */
    if (HDR->flags & STUB_FLAG_EXTEND_LENGTH) {
        int real = length < 27 ? length : 27;
        if (transfer->actual_length < real)
            transfer->actual_length = real;
    }
}

static void translate_callback(struct libusb_transfer *transfer) {
    libusb_transfer_cb_fn orig_cb = NULL;
    device_type_t type = DEV_NONE;
    ps3_rb_guitar_report report;
    int have_input = 0;

    trace(STUB_TRACE_CB_ENTER, (uint32_t)transfer->status, (uint32_t)transfer->length, (uint32_t)transfer->actual_length);
    lock_acquire();
    tracked_transfer_t *tracked = find_transfer_locked(transfer);
    if (tracked != NULL)
        orig_cb = tracked->orig_cb;
    open_device_t *device = find_device_locked(transfer->dev_handle);
    if (device != NULL)
        type = device->type;
    trace(STUB_TRACE_CB_STATE, (uint32_t)type, tracked != NULL, 0);

    if (device != NULL && transfer->status == LIBUSB_TRANSFER_COMPLETED &&
        transfer->buffer != NULL && transfer->length >= 2) {
        const uint8_t *src = transfer->buffer;

        if (type == DEV_XINPUT_WIRELESS) {
            /*
                The receiver sometimes sends status packets between input
                packets, so remember the last real one (byte 1 == 0x01) and
                translate that. The 360 input packet starts after a 4 byte
                header.
            */
            int n = transfer->length < WIRELESS_REPORT_MAX ? transfer->length : WIRELESS_REPORT_MAX;
            if (src[1] == 0x01)
                memcpy(device->last_report, src, (size_t)n);
            if (device->last_report[1] == 0x01 && device->last_report[4] == 0x00) {
                xinput_report_controls controls;
                memset(&controls, 0, sizeof(controls));
                int copy = n - 4;
                if (copy > (int)sizeof(controls))
                    copy = (int)sizeof(controls);
                if (copy > 0)
                    memcpy(&controls, device->last_report + 4, (size_t)copy);
                translate_xinput_guitar(&controls, HDR->flags, &report);
                have_input = 1;
            }
        } else if (type == DEV_XINPUT_GUITAR) {
            if (src[0] == 0x00 && transfer->length >= (int)sizeof(xinput_report_controls)) {
                xinput_report_controls controls;
                memcpy(&controls, src, sizeof(controls));
                translate_xinput_guitar(&controls, HDR->flags, &report);
                have_input = 1;
            }
        }

        if (!have_input)
            translate_neutral_guitar(&report);
        finish_transfer(transfer, &report);
        stat_inc(&HDR->stat_reports);
        trace(STUB_TRACE_CB_REPORT, (uint32_t)report.buttons | ((uint32_t)report.hat << 16),
              (uint32_t)report.whammy | ((uint32_t)report.accel_x << 8), (uint32_t)transfer->actual_length);
    }
    lock_release();

    if (orig_cb != NULL) {
        trace(STUB_TRACE_CB_CALL, (uint32_t)(uintptr_t)orig_cb, (uint32_t)((uint64_t)(uintptr_t)orig_cb >> 32), 0);
        orig_cb(transfer);
        trace(STUB_TRACE_CB_RETURN, 0, 0, 0);
    }
}

/* ---- the hooks ---- */

int stub_hook_open(libusb_device *device, libusb_device_handle **out_handle) {
    stat_inc(&HDR->stat_hook_calls);
    int r = ORIG(fn_open_t, orig_open)(device, out_handle);
    if (r == 0 && out_handle != NULL && *out_handle != NULL) {
        device_type_t type = identify_device(device);
        if (type != DEV_NONE) {
            lock_acquire();
            open_device_t *slot = alloc_device_locked(*out_handle);
            if (slot != NULL) {
                slot->type = type;
                stat_inc(&HDR->stat_guitars_opened);
            }
            lock_release();
        }
        trace(STUB_TRACE_OPEN, (uint32_t)r, (uint32_t)type, 0);
    }
    return r;
}

void stub_hook_close(libusb_device_handle *handle) {
    stat_inc(&HDR->stat_hook_calls);
    trace(STUB_TRACE_CLOSE, 0, 0, 0);
    if (handle != NULL) {
        lock_acquire();
        open_device_t *device = find_device_locked(handle);
        if (device != NULL)
            memset(device, 0, sizeof(*device));
        for (int i = 0; i < MAX_TRANSFERS; i++) {
            if (g_transfers[i].handle == handle)
                memset(&g_transfers[i], 0, sizeof(g_transfers[i]));
        }
        lock_release();
    }
    ORIG(fn_close_t, orig_close)(handle);
}

int stub_hook_get_config(libusb_device *device, uint8_t index, struct libusb_config_descriptor **config) {
    stat_inc(&HDR->stat_hook_calls);
    int r = ORIG(fn_get_config_t, orig_get_config)(device, index, config);
    /* RB4 only talks to HID class devices: present XInput instruments as HID. */
    int presented = 0;
    if (r == 0 && config != NULL && *config != NULL && classify_config(*config) != DEV_NONE) {
        (*config)->interface->altsetting[0].bInterfaceClass = USB_CLASS_HID;
        presented = 1;
    }
    trace(STUB_TRACE_GET_CONFIG, (uint32_t)r, (uint32_t)presented, 0);
    return r;
}

int stub_hook_get_device(libusb_device *device, struct libusb_device_descriptor *desc) {
    stat_inc(&HDR->stat_hook_calls);
    int r = ORIG(fn_get_device_t, orig_get_device)(device, desc);
    int spoofed = 0;
    if (r == 0 && desc != NULL) {
        if (desc->bDeviceClass == 0xFF && desc->bDeviceSubClass == 0xFF && desc->bDeviceProtocol == 0xFF) {
            /* XInput device: detect instruments from the interface descriptor so third party ones work too */
            if (identify_device(device) != DEV_NONE) {
                desc->idVendor = RB4_VENDOR_ID;
                desc->idProduct = RB4_PRODUCT_GUITAR;
                spoofed = 1;
            }
        } else if (desc->idVendor == HARMONIX_VENDOR_ID && desc->idProduct == WII_PRODUCT_GUITAR) {
            /* Wii guitar: already a HID device, it only needs the licensed IDs */
            desc->idVendor = RB4_VENDOR_ID;
            desc->idProduct = RB4_PRODUCT_GUITAR;
            spoofed = 1;
        }
        trace(STUB_TRACE_GET_DEVICE, desc->idVendor, desc->idProduct, (uint32_t)spoofed);
    }
    return r;
}

void stub_hook_fill_interrupt(struct libusb_transfer *transfer, libusb_device_handle *handle,
                              unsigned char endpoint, unsigned char *buffer, int length,
                              libusb_transfer_cb_fn callback, void *user_data, unsigned int timeout) {
    stat_inc(&HDR->stat_hook_calls);
    int wrapped = 0;
    if ((endpoint & USB_ENDPOINT_IN) != 0 && callback != NULL && callback != translate_callback) {
        lock_acquire();
        open_device_t *device = find_device_locked(handle);
        if (device != NULL) {
            tracked_transfer_t *tracked = alloc_transfer_locked(transfer);
            if (tracked != NULL) {
                tracked->transfer = transfer;
                tracked->handle = handle;
                tracked->orig_cb = callback;
                callback = translate_callback;
                wrapped = 1;
            } else {
                stat_inc(&HDR->stat_dropped);
            }
        }
        lock_release();
    }
    trace(STUB_TRACE_FILL, endpoint, (uint32_t)length, (uint32_t)wrapped);
    ORIG(fn_fill_interrupt_t, orig_fill_interrupt)(transfer, handle, endpoint, buffer, length, callback,
                                                    user_data, timeout);
}

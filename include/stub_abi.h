/*
    stub_abi.h - PS5Instrumentalizer
    Layout shared between the PS5-side injector and the hook stub that runs
    inside the Rock Band 4 process. The stub image starts with a
    stub_header_t; the injector fills in the "orig_*" function pointers
    before redirecting the game's sceUsbd imports to the "hook_*" entries.

    Licensed under the GNU Lesser General Public License version 2.1, or later.
*/

#ifndef PS5I_STUB_ABI_H
#define PS5I_STUB_ABI_H

#include <stdint.h>

#define STUB_MAGIC       0x3149355Au /* "Z5I1" little endian */
#define STUB_ABI_VERSION 2

/* config flags (stub_header_t::flags) */
#define STUB_FLAG_TILT_LEGACY    (1u << 0) /* use the original OrbisInstrumentalizer tilt mapping */
#define STUB_FLAG_WHAMMY_LEGACY  (1u << 1) /* use the original OrbisInstrumentalizer whammy mapping */
#define STUB_FLAG_EXTEND_LENGTH  (1u << 2) /* report a full-sized PS3 report in transfer->actual_length */

/* trace event codes (stub_trace_event_t::code) */
#define STUB_TRACE_OPEN        0x01 /* a=orig result, b=device type */
#define STUB_TRACE_CLOSE       0x02
#define STUB_TRACE_GET_CONFIG  0x03 /* a=orig result, b=1 if presented as HID */
#define STUB_TRACE_GET_DEVICE  0x04 /* a=vid, b=pid, c=1 if spoofed */
#define STUB_TRACE_FILL        0x05 /* a=endpoint, b=length, c=1 if wrapped */
#define STUB_TRACE_CB_ENTER    0x10 /* a=status, b=length, c=actual_length */
#define STUB_TRACE_CB_STATE    0x11 /* a=device type (0=untracked), b=1 if transfer tracked */
#define STUB_TRACE_CB_REPORT   0x12 /* a=buttons | hat << 16, b=whammy | tilt << 8, c=new actual_length */
#define STUB_TRACE_CB_CALL     0x13 /* a/b = low/high 32 bits of the game's callback */
#define STUB_TRACE_CB_RETURN   0x14

#define STUB_TRACE_SLOTS 64

typedef struct stub_trace_event {
    uint32_t code;
    uint32_t a;
    uint32_t b;
    uint32_t c;
} stub_trace_event_t;

typedef struct stub_header {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t flags;       /* STUB_FLAG_*, set by the injector */
    uint32_t reserved;
    uint64_t image_end;   /* total size of the image in memory, including .bss (set at link time) */

    /* Entry points, as offsets from the start of the image (set at link time). */
    uint64_t hook_open;
    uint64_t hook_close;
    uint64_t hook_get_config;
    uint64_t hook_get_device;
    uint64_t hook_fill_interrupt;

    /* Real libSceUsbd functions, absolute addresses in the game process (set by the injector). */
    uint64_t orig_open;
    uint64_t orig_close;
    uint64_t orig_get_config;
    uint64_t orig_free_config;
    uint64_t orig_get_device;
    uint64_t orig_fill_interrupt;

    /* Diagnostics, written by the stub, read by the daemon. */
    uint32_t stat_hook_calls;     /* any hook entered */
    uint32_t stat_guitars_opened; /* devices recognised as instruments */
    uint32_t stat_reports;        /* input reports translated */
    uint32_t stat_dropped;        /* transfers we could not track */

    /* Ring buffer of the last events, so a crash can be traced from outside the game. */
    uint32_t trace_head;          /* total number of events written */
    uint32_t trace_reserved[3];
    stub_trace_event_t trace[STUB_TRACE_SLOTS];
} stub_header_t;

#define STUB_HEADER_OFFSET_OF(field) ((uint64_t)__builtin_offsetof(stub_header_t, field))

#endif

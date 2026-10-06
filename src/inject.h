/*
    inject.h - PS5Instrumentalizer
    Injects the sceUsbd hook stub into a running Rock Band 4 process and
    redirects the game's sceUsbd imports to it.

    Licensed under the GNU Lesser General Public License version 2.1, or later.
*/

#ifndef PS5I_INJECT_H
#define PS5I_INJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "stub_abi.h"
#include "versions.h"

typedef enum inject_result {
    INJECT_OK = 0,
    INJECT_NOT_READY = -1,   /* game not far enough along yet (libSceUsbd / eboot not mapped), retry later */
    INJECT_UNSUPPORTED = -2, /* validation failed, nothing was modified */
    INJECT_FAILED = -3       /* an operation failed; any partial changes were rolled back */
} inject_result_t;

#define INJECT_MAX_PATCHES 24

typedef struct inject_patch {
    intptr_t addr;
    size_t len;
    uint8_t original[16];
} inject_patch_t;

typedef struct injection {
    bool active;
    pid_t pid;
    intptr_t stub_base;
    size_t stub_size;
    int num_patches;
    inject_patch_t patches[INJECT_MAX_PATCHES];
} injection_t;

/*
    Injects into `pid`. `known` may be NULL (unknown version): only the
    dynamic import scan is attempted in that case.
*/
inject_result_t inject_stub(pid_t pid, const known_version_t *known, uint32_t flags, injection_t *inj);

/* Restores the game's original imports. The stub memory itself is left mapped (callbacks may still be running in it). */
void inject_rollback(injection_t *inj);

/* Reads the stub's diagnostic counters. Returns 0 on success. */
int inject_read_stats(const injection_t *inj, stub_header_t *out);

/* Reads the process's 9 character title ID (e.g. "CUSA02084"). Returns 0 on success. */
int inject_get_title_id(pid_t pid, char out[16]);

/* True if the process still exists. */
bool inject_process_alive(pid_t pid);

#endif

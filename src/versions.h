/*
    versions.h - PS5Instrumentalizer
    Supported Rock Band 4 title IDs and the known import-thunk offsets of
    version 02.21 (used only as a fallback when the dynamic scan of the
    game's import slots finds nothing).
*/

#ifndef PS5I_VERSIONS_H
#define PS5I_VERSIONS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct known_version {
    const char *title_id;
    const char *app_version;
    /* offsets of the game's sceUsbd import thunks, relative to the eboot base */
    uint64_t thunk_open;
    uint64_t thunk_close;
    uint64_t thunk_get_config;
    uint64_t thunk_get_device;
    uint64_t thunk_fill_interrupt;
} known_version_t;

/* True if `title_id` is Rock Band 4 (CUSA02084 USA, CUSA02901 PAL). */
bool versions_is_target_title(const char *title_id);

/* Returns the table entry for this exact title and APP_VER, or NULL. */
const known_version_t *versions_find(const char *title_id, const char *app_version);

/* Reads APP_VER from the game's param.sfo. Returns 0 on success. */
int versions_read_app_version(const char *title_id, char *out, unsigned out_size);

/* Parses APP_VER out of a param.sfo image in memory. Exposed for testing. Returns 0 on success. */
int versions_parse_sfo_app_version(const uint8_t *sfo, unsigned size, char *out, unsigned out_size);

#endif

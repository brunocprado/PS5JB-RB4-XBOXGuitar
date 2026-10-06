/*
    versions.c - PS5Instrumentalizer
    Licensed under the GNU Lesser General Public License version 2.1, or later.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "versions.h"

/*
    Offsets taken from OrbisInstrumentalizer (usbd_hooks_rb4.c), valid for
    version 02.21 (in-game 2.3.7) of the PS4 game: each one is a 16 byte
    PLT-style thunk ("jmp [rip+x]") for the corresponding sceUsbd import.
*/
#define RB4_0221_THUNK_OPEN            0x01245260ull
#define RB4_0221_THUNK_GET_DEVICE      0x01245210ull
#define RB4_0221_THUNK_GET_CONFIG      0x012451e0ull
#define RB4_0221_THUNK_FILL_INTERRUPT  0x01245190ull
#define RB4_0221_THUNK_CLOSE           0x01245160ull

static const known_version_t known_versions[] = {
    { "CUSA02084", "02.21", RB4_0221_THUNK_OPEN, RB4_0221_THUNK_CLOSE, RB4_0221_THUNK_GET_CONFIG,
      RB4_0221_THUNK_GET_DEVICE, RB4_0221_THUNK_FILL_INTERRUPT },
    { "CUSA02901", "02.21", RB4_0221_THUNK_OPEN, RB4_0221_THUNK_CLOSE, RB4_0221_THUNK_GET_CONFIG,
      RB4_0221_THUNK_GET_DEVICE, RB4_0221_THUNK_FILL_INTERRUPT },
};

static const char *target_titles[] = { "CUSA02084", "CUSA02901" };

bool versions_is_target_title(const char *title_id) {
    if (title_id == NULL)
        return false;
    for (unsigned i = 0; i < sizeof(target_titles) / sizeof(target_titles[0]); i++) {
        if (strcmp(title_id, target_titles[i]) == 0)
            return true;
    }
    return false;
}

const known_version_t *versions_find(const char *title_id, const char *app_version) {
    if (title_id == NULL || app_version == NULL)
        return NULL;
    for (unsigned i = 0; i < sizeof(known_versions) / sizeof(known_versions[0]); i++) {
        if (strcmp(known_versions[i].title_id, title_id) == 0 && strcmp(known_versions[i].app_version, app_version) == 0)
            return &known_versions[i];
    }
    return NULL;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int versions_parse_sfo_app_version(const uint8_t *sfo, unsigned size, char *out, unsigned out_size) {
    if (sfo == NULL || size < 20 || out == NULL || out_size == 0)
        return -1;
    if (memcmp(sfo, "\0PSF", 4) != 0)
        return -1;

    uint32_t key_table = rd32(sfo + 8);
    uint32_t data_table = rd32(sfo + 12);
    uint32_t count = rd32(sfo + 16);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t entry = 20 + i * 16;
        if (entry + 16 > size)
            return -1;
        uint32_t key_off = key_table + rd16(sfo + entry);
        uint32_t data_len = rd32(sfo + entry + 4);
        uint32_t data_off = data_table + rd32(sfo + entry + 12);
        if (key_off >= size || data_off >= size)
            continue;
        if (strncmp((const char *)sfo + key_off, "APP_VER", size - key_off) != 0)
            continue;
        if (data_off + data_len > size)
            return -1;

        unsigned n = data_len < out_size - 1 ? data_len : out_size - 1;
        memcpy(out, sfo + data_off, n);
        out[n] = '\0';
        /* strip trailing NULs/whitespace */
        while (n > 0 && (out[n - 1] == '\0' || out[n - 1] == ' '))
            out[--n] = '\0';
        return n > 0 ? 0 : -1;
    }
    return -1;
}

int versions_read_app_version(const char *title_id, char *out, unsigned out_size) {
    static const char *templates[] = {
        "/system_data/priv/appmeta/%s/param.sfo",
        "/user/appmeta/%s/param.sfo",
        "/user/app/%s/sce_sys/param.sfo",
        "/mnt/sandbox/%s_000/app0/sce_sys/param.sfo",
    };

    for (unsigned i = 0; i < sizeof(templates) / sizeof(templates[0]); i++) {
        char path[160];
        snprintf(path, sizeof(path), templates[i], title_id);
        FILE *f = fopen(path, "rb");
        if (f == NULL)
            continue;
        uint8_t buf[4096];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        if (versions_parse_sfo_app_version(buf, (unsigned)n, out, out_size) == 0)
            return 0;
    }
    return -1;
}

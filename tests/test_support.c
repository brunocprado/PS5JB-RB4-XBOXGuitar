/*
    test_support.c - PS5Instrumentalizer
    Host-side tests for config parsing and param.sfo / version handling.
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "config.h"
#include "versions.h"
#include "stub_abi.h"

#include <stdarg.h>

/* host replacement for src/log.c */
void log_write(const char *fmt, ...) {
    (void)fmt;
}

static int failures = 0;
static int checks = 0;

#define CHECK_EQ(actual, expected, what)                                              \
    do {                                                                              \
        checks++;                                                                     \
        long a_ = (long)(actual), e_ = (long)(expected);                              \
        if (a_ != e_) {                                                               \
            failures++;                                                               \
            printf("FAIL %s:%d %s: got %ld expected %ld\n", __FILE__, __LINE__,       \
                   what, a_, e_);                                                     \
        }                                                                             \
    } while (0)

static void defaults(config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->notifications = true;
    cfg->poll_ms = 1000;
}

static void test_config(void) {
    config_t cfg;

    defaults(&cfg);
    config_parse(&cfg, "");
    CHECK_EQ(cfg.stub_flags, 0, "empty config flags");
    CHECK_EQ(cfg.notifications, 1, "notifications default");
    CHECK_EQ(cfg.poll_ms, 1000, "poll default");

    defaults(&cfg);
    config_parse(&cfg,
                 "; comment\n"
                 "[section]\n"
                 "tilt_legacy = 1\r\n"
                 "whammy_legacy=true\n"
                 "notifications = off\n"
                 "stats_logging=yes\n"
                 "poll_ms=500\n");
    CHECK_EQ(cfg.stub_flags, STUB_FLAG_TILT_LEGACY | STUB_FLAG_WHAMMY_LEGACY, "legacy flags");
    CHECK_EQ(cfg.notifications, 0, "notifications off");
    CHECK_EQ(cfg.stats_logging, 1, "stats on");
    CHECK_EQ(cfg.poll_ms, 500, "poll override");

    defaults(&cfg);
    config_parse(&cfg, "poll_ms=5\ntilt_legacy=maybe\nunknown=1\nnoequals\n");
    CHECK_EQ(cfg.poll_ms, 1000, "poll out of range ignored");
    CHECK_EQ(cfg.stub_flags, 0, "invalid bool ignored");
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static unsigned build_sfo(uint8_t *buf, const char *app_ver) {
    /* two entries: TITLE_ID and APP_VER */
    memset(buf, 0, 512);
    memcpy(buf, "\0PSF", 4);
    put32(buf + 4, 0x101);
    const uint32_t key_table = 20 + 2 * 16;
    const char keys[] = "TITLE_ID\0APP_VER\0";
    const uint32_t data_table = key_table + 32;
    put32(buf + 8, key_table);
    put32(buf + 12, data_table);
    put32(buf + 16, 2);

    uint8_t *e0 = buf + 20;
    e0[0] = 0; e0[1] = 0;                  /* key offset 0 */
    put32(e0 + 4, 10); put32(e0 + 8, 16);  /* len, max len */
    put32(e0 + 12, 0);                     /* data offset */

    uint8_t *e1 = buf + 36;
    e1[0] = 9; e1[1] = 0;                  /* key offset of APP_VER */
    put32(e1 + 4, (uint32_t)strlen(app_ver) + 1); put32(e1 + 8, 8);
    put32(e1 + 12, 16);

    memcpy(buf + key_table, keys, sizeof(keys));
    memcpy(buf + data_table, "CUSA02084", 10);
    memcpy(buf + data_table + 16, app_ver, strlen(app_ver) + 1);
    return data_table + 32;
}

static void test_versions(void) {
    CHECK_EQ(versions_is_target_title("CUSA02084"), 1, "usa title");
    CHECK_EQ(versions_is_target_title("CUSA02901"), 1, "pal title");
    CHECK_EQ(versions_is_target_title("CUSA00001"), 0, "other title");
    CHECK_EQ(versions_is_target_title(NULL), 0, "null title");

    CHECK_EQ(versions_find("CUSA02084", "02.21") != NULL, 1, "known usa 02.21");
    CHECK_EQ(versions_find("CUSA02901", "02.21") != NULL, 1, "known pal 02.21");
    CHECK_EQ(versions_find("CUSA02084", "02.22") == NULL, 1, "unknown version");
    CHECK_EQ(versions_find("CUSA02084", "02.21")->thunk_open, 0x01245260, "thunk offset");

    uint8_t sfo[512];
    char ver[16];
    unsigned size = build_sfo(sfo, "02.21");
    CHECK_EQ(versions_parse_sfo_app_version(sfo, size, ver, sizeof(ver)), 0, "parse sfo");
    CHECK_EQ(strcmp(ver, "02.21"), 0, "sfo version string");

    CHECK_EQ(versions_parse_sfo_app_version(sfo, 10, ver, sizeof(ver)), -1, "truncated sfo");
    sfo[1] = 'X';
    CHECK_EQ(versions_parse_sfo_app_version(sfo, size, ver, sizeof(ver)), -1, "bad magic");
}

int main(void) {
    test_config();
    test_versions();
    printf("test_support: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

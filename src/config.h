/*
    config.h - PS5Instrumentalizer
    Optional settings read from /data/PS5Instrumentalizer/config.ini
*/

#ifndef PS5I_CONFIG_H
#define PS5I_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define CONFIG_PATH "/data/PS5Instrumentalizer/config.ini"

typedef struct config {
    uint32_t stub_flags;   /* STUB_FLAG_* from stub_abi.h */
    bool notifications;    /* show on-screen notifications */
    bool stats_logging;    /* periodically log stub statistics */
    bool crash_catch;      /* stay attached as a debugger and log crashes of the game */
    bool trace_log;        /* log the stub's event trace while the game runs */
    int poll_ms;           /* process polling interval while idle */
} config_t;

/* Fills `cfg` with defaults, then overrides them with values found in CONFIG_PATH (if present). */
void config_load(config_t *cfg);

/* Parses ini text. Exposed for testing. */
void config_parse(config_t *cfg, const char *text);

#endif

/*
    config.c - PS5Instrumentalizer
    Tiny key=value parser for /data/PS5Instrumentalizer/config.ini

    Supported keys (values: 1/0, true/false, yes/no, on/off):
      tilt_legacy      use the original OrbisInstrumentalizer tilt mapping
      whammy_legacy    use the original OrbisInstrumentalizer whammy mapping
      notifications    on-screen notifications (default on)
      stats_logging    log stub statistics every few seconds (default off)
      extend_length    report a PS3 sized report in actual_length (default off)
      crash_catch      attach as a debugger and log game crashes (default off)
      trace_log        log the stub's event trace (default off)
      poll_ms          process polling interval in milliseconds (default 1000)
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"
#include "stub_abi.h"

static bool parse_bool(const char *value, bool fallback) {
    if (!strcasecmp(value, "1") || !strcasecmp(value, "true") || !strcasecmp(value, "yes") || !strcasecmp(value, "on"))
        return true;
    if (!strcasecmp(value, "0") || !strcasecmp(value, "false") || !strcasecmp(value, "no") || !strcasecmp(value, "off"))
        return false;
    return fallback;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static void set_flag(config_t *cfg, uint32_t flag, bool on) {
    if (on)
        cfg->stub_flags |= flag;
    else
        cfg->stub_flags &= ~flag;
}

static void config_defaults(config_t *cfg) {
    cfg->stub_flags = 0;
    cfg->notifications = true;
    cfg->stats_logging = false;
    cfg->crash_catch = false;
    cfg->trace_log = false;
    cfg->poll_ms = 1000;
}

void config_parse(config_t *cfg, const char *text) {
    char *copy = strdup(text);
    if (copy == NULL)
        return;

    char *save = NULL;
    for (char *line = strtok_r(copy, "\r\n", &save); line != NULL; line = strtok_r(NULL, "\r\n", &save)) {
        line = trim(line);
        if (*line == '\0' || *line == ';' || *line == '#' || *line == '[')
            continue;
        char *eq = strchr(line, '=');
        if (eq == NULL)
            continue;
        *eq = '\0';
        char *key = trim(line);
        char *value = trim(eq + 1);

        if (!strcasecmp(key, "tilt_legacy"))
            set_flag(cfg, STUB_FLAG_TILT_LEGACY, parse_bool(value, false));
        else if (!strcasecmp(key, "whammy_legacy"))
            set_flag(cfg, STUB_FLAG_WHAMMY_LEGACY, parse_bool(value, false));
        else if (!strcasecmp(key, "extend_length"))
            set_flag(cfg, STUB_FLAG_EXTEND_LENGTH, parse_bool(value, false));
        else if (!strcasecmp(key, "crash_catch"))
            cfg->crash_catch = parse_bool(value, cfg->crash_catch);
        else if (!strcasecmp(key, "trace_log"))
            cfg->trace_log = parse_bool(value, cfg->trace_log);
        else if (!strcasecmp(key, "notifications"))
            cfg->notifications = parse_bool(value, cfg->notifications);
        else if (!strcasecmp(key, "stats_logging"))
            cfg->stats_logging = parse_bool(value, cfg->stats_logging);
        else if (!strcasecmp(key, "poll_ms")) {
            int ms = atoi(value);
            if (ms >= 100 && ms <= 10000)
                cfg->poll_ms = ms;
        }
    }
    free(copy);
}

void config_load(config_t *cfg) {
    config_defaults(cfg);

    FILE *f = fopen(CONFIG_PATH, "r");
    if (f == NULL) {
        LOG("no config at %s, using defaults", CONFIG_PATH);
        return;
    }
    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    config_parse(cfg, buf);
    LOG("config: flags=0x%x notifications=%d stats=%d crash_catch=%d trace=%d poll=%dms", cfg->stub_flags,
        cfg->notifications, cfg->stats_logging, cfg->crash_catch, cfg->trace_log, cfg->poll_ms);
}

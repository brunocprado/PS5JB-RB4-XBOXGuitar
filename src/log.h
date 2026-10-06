/*
    log.h - PS5Instrumentalizer
    Logging helpers. Every line goes to the kernel log (etaHEN exposes it on
    port 9081) and is appended to LOG_PATH, which can be read over FTP when
    no klog viewer is available.
*/

#ifndef PS5I_LOG_H
#define PS5I_LOG_H

#define PLUGIN_NAME "PS5Instrumentalizer"
#define LOG_DIR     "/data/PS5Instrumentalizer"
#define LOG_PATH    LOG_DIR "/log.txt"

void log_init(void);
void log_write(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOG(fmt, ...) log_write(fmt, ##__VA_ARGS__)

#endif

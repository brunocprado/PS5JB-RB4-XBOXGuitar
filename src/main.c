/*
    main.c - PS5Instrumentalizer
    PS5 payload / etaHEN plugin that lets Xbox 360 Rock Band and Guitar Hero
    guitars work in Rock Band 4 (PS4 version running on the PS5).

    Based on OrbisInstrumentalizer by InvoxiPlayGames (PS4 / GoldHEN).
    Licensed under the GNU Lesser General Public License version 2.1, or later.

    The payload runs as a small daemon: it watches for the Rock Band 4
    process, injects the sceUsbd hook stub once the game is far enough along
    (see inject.c) and removes its patches when the game exits or the payload
    is stopped.
*/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/user.h>

#include <ps5/kernel.h>

#include "config.h"
#include "crash.h"
#include "inject.h"
#include "log.h"
#include "proc_util.h"
#include "versions.h"

#define PLUGIN_VERSION "1.0"

#define MAX_TARGETS          4
#define MAX_NOT_READY        1200 /* ~5 minutes at 250 ms */
#define MAX_HARD_FAILURES    120
#define FAST_POLL_MS         250
#define TRACE_POLL_MS        20
#define TRACE_BUDGET         300 /* events logged per game run */
#define CONTROL_PORT         9041 /* single instance guard / "please quit" channel, localhost only */
#define ETAHEN_IPC_PORT      9028
#define ETAHEN_IPC_MAGIC     0xDEADBEEF
#define ETAHEN_JAILBREAK_CMD 6

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

/* ------------------------------------------------------------------ */
/* notifications                                                       */
/* ------------------------------------------------------------------ */

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const config_t *cfg, const char *text) {
    LOG("%s", text);
    if (!cfg->notifications)
        return;
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.message, sizeof(req.message), "%s", text);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* ------------------------------------------------------------------ */
/* privileges                                                          */
/* ------------------------------------------------------------------ */

typedef struct etahen_cmd {
    int magic;
    int cmd;
    int pid;
    int ret;
    char msg1[0x500];
    char msg2[0x500];
} etahen_cmd_t;

/*
    When loaded as an etaHEN plugin the process may still need to ask the
    etaHEN daemon (localhost:9028) to jailbreak it. Payloads sent straight to
    the ELF loader are already privileged, so a failure here is not fatal.
*/
static void etahen_self_jailbreak(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return;

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ETAHEN_IPC_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        LOG("etaHEN IPC not reachable (%s), assuming the payload is already privileged", strerror(errno));
        close(fd);
        return;
    }

    etahen_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.magic = (int)ETAHEN_IPC_MAGIC;
    cmd.cmd = ETAHEN_JAILBREAK_CMD;
    cmd.pid = getpid();
    cmd.ret = -1337;
    if (send(fd, &cmd, sizeof(cmd), 0) == (ssize_t)sizeof(cmd) && recv(fd, &cmd, sizeof(cmd), MSG_WAITALL) > 0)
        LOG("etaHEN jailbreak request returned %d", cmd.ret);
    else
        LOG("etaHEN jailbreak request got no answer");
    close(fd);
}

/* ------------------------------------------------------------------ */
/* single instance                                                     */
/* ------------------------------------------------------------------ */

static int bind_control_socket(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONTROL_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 1) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Asks a running instance to quit so that a new version of the payload can take over. */
static void ask_previous_instance_to_quit(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONTROL_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
        (void)!send(fd, "quit", 4, 0);
    close(fd);
}

static void control_poll(int fd) {
    if (fd < 0)
        return;
    struct timeval tv = { 0, 0 };
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    if (select(fd + 1, &set, NULL, NULL, &tv) > 0) {
        int client = accept(fd, NULL, NULL);
        if (client >= 0) {
            close(client);
            LOG("another instance asked this one to quit");
            g_stop = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* game tracking                                                       */
/* ------------------------------------------------------------------ */

typedef struct tracked_game {
    bool used;
    pid_t pid;
    char title_id[16];
    char app_version[16];
    injection_t inj;
    int not_ready;
    int hard_failures;
    bool gave_up;
    stub_header_t last_stats;
    time_t last_stats_time;
    bool watching;
    uint32_t trace_seen;
    int trace_budget;
} tracked_game_t;

static tracked_game_t g_games[MAX_TARGETS];

static tracked_game_t *find_game(pid_t pid) {
    for (int i = 0; i < MAX_TARGETS; i++) {
        if (g_games[i].used && g_games[i].pid == pid)
            return &g_games[i];
    }
    return NULL;
}

static tracked_game_t *add_game(pid_t pid, const char *title_id) {
    for (int i = 0; i < MAX_TARGETS; i++) {
        if (!g_games[i].used) {
            memset(&g_games[i], 0, sizeof(g_games[i]));
            g_games[i].used = true;
            g_games[i].pid = pid;
            snprintf(g_games[i].title_id, sizeof(g_games[i].title_id), "%s", title_id);
            return &g_games[i];
        }
    }
    return NULL;
}

static void flush_trace(tracked_game_t *g) {
    if (g->inj.active && g->trace_budget > 0)
        g->trace_budget -= trace_dump_new(g->pid, g->inj.stub_base, &g->trace_seen, g->trace_budget);
}

static void forget_game(tracked_game_t *g) {
    if (g->watching)
        crash_watch_stop(g->pid);
    if (g->inj.active && inject_process_alive(g->pid))
        inject_rollback(&g->inj);
    memset(g, 0, sizeof(*g));
}

/* Collects the PIDs of running Rock Band 4 processes. */
static int find_target_processes(pid_t *pids, char titles[][16], int max) {
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0)
        return 0;
    size += size / 8 + 4096;
    uint8_t *buf = malloc(size);
    if (buf == NULL)
        return 0;
    if (sysctl(mib, 4, buf, &size, NULL, 0) != 0) {
        free(buf);
        return 0;
    }

    int found = 0;
    pid_t me = getpid();
    for (uint8_t *ptr = buf; ptr + sizeof(int) <= buf + size;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)ptr;
        if (ki->ki_structsize <= 0)
            break;
        ptr += ki->ki_structsize;
        if (ki->ki_pid == me || ki->ki_pid <= 1)
            continue;

        char title[16];
        if (inject_get_title_id(ki->ki_pid, title) != 0 || !versions_is_target_title(title))
            continue;
        if (found < max) {
            pids[found] = ki->ki_pid;
            snprintf(titles[found], 16, "%s", title);
            found++;
        }
    }
    free(buf);
    return found;
}

static void try_inject(tracked_game_t *g, const config_t *cfg) {
    if (g->app_version[0] == '\0') {
        if (versions_read_app_version(g->title_id, g->app_version, sizeof(g->app_version)) != 0)
            snprintf(g->app_version, sizeof(g->app_version), "unknown");
        LOG("%s pid %d version %s", g->title_id, g->pid, g->app_version);
    }

    const known_version_t *known = versions_find(g->title_id, g->app_version);
    inject_result_t r = inject_stub(g->pid, known, cfg->stub_flags, &g->inj);

    switch (r) {
        case INJECT_OK:
            notify(cfg, "PS5Instrumentalizer: Xbox 360 guitar support active");
            g->trace_budget = cfg->trace_log ? TRACE_BUDGET : 0;
            if (cfg->crash_catch && crash_watch_start(g->pid, g->inj.stub_base, g->inj.stub_size) == 0)
                g->watching = true;
            break;
        case INJECT_NOT_READY:
            if (++g->not_ready >= MAX_NOT_READY) {
                g->gave_up = true;
                notify(cfg, "PS5Instrumentalizer: Rock Band 4 never loaded libSceUsbd, giving up");
            } else if (g->not_ready == 1 || g->not_ready % 40 == 0) {
                LOG("pid %d: waiting for the game to load libSceUsbd (%d)", g->pid, g->not_ready);
            }
            break;
        case INJECT_UNSUPPORTED:
        case INJECT_FAILED:
            if (++g->hard_failures >= MAX_HARD_FAILURES) {
                g->gave_up = true;
                notify(cfg, "PS5Instrumentalizer: could not hook Rock Band 4 (see klog), nothing was changed");
            } else if (g->hard_failures == 1 || g->hard_failures % 10 == 0) {
                LOG("pid %d: injection %s (attempt %d), will retry", g->pid,
                    r == INJECT_UNSUPPORTED ? "not possible yet" : "failed", g->hard_failures);
            }
            break;
    }
}

static void log_stats(tracked_game_t *g) {
    stub_header_t stats;
    if (inject_read_stats(&g->inj, &stats) != 0)
        return;
    if (stats.stat_hook_calls != g->last_stats.stat_hook_calls || stats.stat_reports != g->last_stats.stat_reports ||
        stats.stat_guitars_opened != g->last_stats.stat_guitars_opened) {
        LOG("pid %d stats: hook calls=%u instruments opened=%u reports=%u untracked transfers=%u", g->pid,
            stats.stat_hook_calls, stats.stat_guitars_opened, stats.stat_reports, stats.stat_dropped);
        g->last_stats = stats;
    }
}

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* One pass over the running processes. Returns true while an injection is still pending. */
static bool scan_games(const config_t *cfgp) {
    const config_t cfg = *cfgp;
    pid_t pids[MAX_TARGETS];
    char titles[MAX_TARGETS][16];
    int count = find_target_processes(pids, titles, MAX_TARGETS);

    /* forget games that went away */
    for (int i = 0; i < MAX_TARGETS; i++) {
        tracked_game_t *g = &g_games[i];
        if (!g->used)
            continue;
        bool still_running = false;
        for (int j = 0; j < count; j++) {
            if (pids[j] == g->pid)
                still_running = true;
        }
        if (!still_running || !inject_process_alive(g->pid)) {
            LOG("pid %d (%s) is gone", g->pid, g->title_id);
            forget_game(g);
        }
    }

    /* start tracking new ones */
    for (int j = 0; j < count; j++) {
        if (find_game(pids[j]) == NULL && add_game(pids[j], titles[j]) != NULL)
            LOG("found %s (pid %d)", titles[j], pids[j]);
    }

    bool pending = false;
    for (int i = 0; i < MAX_TARGETS; i++) {
        tracked_game_t *g = &g_games[i];
        if (!g->used || g->gave_up)
            continue;
        if (!g->inj.active) {
            try_inject(g, &cfg);
            pending = !g->inj.active && !g->gave_up;
        } else if (cfg.stats_logging && time(NULL) - g->last_stats_time >= 5) {
            g->last_stats_time = time(NULL);
            log_stats(g);
        }
    }

    return pending;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    config_t cfg;

    log_init();
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    LOG("version " PLUGIN_VERSION " starting (firmware 0x%08x, authid 0x%016lx)", kernel_get_fw_version(),
        (unsigned long)kernel_get_ucred_authid(getpid()));

    int control_fd = bind_control_socket();
    if (control_fd < 0) {
        LOG("an instance is already running, asking it to quit");
        ask_previous_instance_to_quit();
        for (int i = 0; i < 20 && control_fd < 0; i++) {
            usleep(250 * 1000);
            control_fd = bind_control_socket();
        }
        if (control_fd < 0) {
            LOG("the previous instance did not quit, exiting");
            return 1;
        }
    }

    config_load(&cfg);
    etahen_self_jailbreak();
    if (cfg.crash_catch && inject_raise_privileges() != 0) {
        LOG("could not raise privileges, the crash watcher is disabled");
        cfg.crash_catch = false;
    }
    notify(&cfg, "PS5Instrumentalizer loaded: start Rock Band 4 with the guitar plugged in");

    long last_scan_ms = -1000000;
    long scan_interval_ms = cfg.poll_ms;

    while (!g_stop) {
        control_poll(control_fd);

        bool tracing = false;
        for (int i = 0; i < MAX_TARGETS; i++) {
            tracked_game_t *g = &g_games[i];
            if (g->used && g->inj.active) {
                flush_trace(g);
                tracing |= g->trace_budget > 0;
            }
        }

        if (now_ms() - last_scan_ms >= scan_interval_ms) {
            last_scan_ms = now_ms();
            bool pending = scan_games(&cfg);
            scan_interval_ms = pending ? FAST_POLL_MS : cfg.poll_ms;
        }

        usleep((tracing ? TRACE_POLL_MS : TRACE_POLL_MS * 5) * 1000);
    }

    LOG("stopping, restoring the game imports");
    for (int i = 0; i < MAX_TARGETS; i++) {
        if (g_games[i].used)
            forget_game(&g_games[i]);
    }
    close(control_fd);
    return 0;
}

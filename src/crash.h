/*
    crash.h - PS5Instrumentalizer
    Optional "black box" for the game: a thread stays attached to the game
    with ptrace and writes a report to the log when the game crashes
    (registers, faulting address, which module the crash happened in, the
    stub's latest events). The signal is then passed on to the game, so its
    behaviour does not change.

    Licensed under the GNU Lesser General Public License version 2.1, or later.
*/

#ifndef PS5I_CRASH_H
#define PS5I_CRASH_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Starts watching `pid`. `stub_base` / `stub_size` describe the injected stub (0 if unknown). */
int crash_watch_start(pid_t pid, intptr_t stub_base, size_t stub_size);

/* Stops watching and detaches. Safe to call when nothing is being watched. */
void crash_watch_stop(pid_t pid);

/* Logs trace events of the stub that have not been logged yet; returns how many were logged. */
int trace_dump_new(pid_t pid, intptr_t stub_base, uint32_t *seen, int budget);

#endif

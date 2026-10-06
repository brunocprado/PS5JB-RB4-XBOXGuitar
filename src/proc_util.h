/*
    proc_util.h - PS5Instrumentalizer
    Helpers shared by the injector and the crash watcher: memory access to
    another process, its memory map, and privileged ptrace.
*/

#ifndef PS5I_PROC_UTIL_H
#define PS5I_PROC_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>

int proc_read(pid_t pid, intptr_t addr, void *buf, size_t len);
int proc_write(pid_t pid, intptr_t addr, const void *buf, size_t len);

/* Memory map of a process; the result must be free()d. */
struct kinfo_vmentry *vmmap_get(pid_t pid, size_t *out_size);

#define VMMAP_FOREACH(e, buf, size)                                                                  \
    for (struct kinfo_vmentry *e = (buf); (uint8_t *)e + sizeof(int) <= (uint8_t *)(buf) + (size) && \
                                          e->kve_structsize > 0 &&                                   \
                                          (uint8_t *)e + e->kve_structsize <= (uint8_t *)(buf) + (size); \
         e = (struct kinfo_vmentry *)((uint8_t *)e + e->kve_structsize))

/* ptrace(2) through the raw syscall. The caller needs raised privileges (see inject_raise_privileges). */
int pu_ptrace(int request, pid_t pid, caddr_t addr, int data);

/*
    Raises this process's SCE authid / capabilities so that ptrace on game
    processes is allowed, and leaves them raised. Returns 0 on success.
*/
int inject_raise_privileges(void);

#endif

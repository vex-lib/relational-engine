#ifndef IO_PROCESS_SPAWN_H
#define IO_PROCESS_SPAWN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"

// io/process_spawn.h — engine-owned R2 bounded child table, supervised by R1.
//
// A bounded child-process job table: fixed slots, per-job pid/exit/done,
// table-level mirrors, cancel flag, and timeout. Spawns via posix_spawnp
// (never system()), reaps via non-blocking waitpid slices bounded to
// 100ms (Bounded Wait Law), cancels via SIGTERM. Arena-tracked, zero threads.

#define PROCESS_SPAWN_JOBS_MAX 8u
#define PROCESS_SPAWN_POLL_MAX_NS 100000000ULL

// SLOT RECORD — one child job row (Single Class Per File Law, zero behavior of
// its own; all behavior hangs off the ProcessSpawn table class).
typedef struct ProcessSpawnJob {
    int32_t pid;      // child pid (> 0 while tracked, 0 = slot free)
    int32_t exitCode; // WEXITSTATUS / -signal once done, 0 until reaped
    bool done;        // true once waitpid has reaped the child
} ProcessSpawnJob;

typedef struct ProcessSpawn {
    int32_t pid;                              // most recent child pid (0 none)
    uint64_t timeoutMs;                       // default spawn budget in ms
    int32_t exitCode;                         // last reaped exit code
    bool cancelFlag;                          // cancel raised; poll degrades
    ProcessSpawnJob jobs[PROCESS_SPAWN_JOBS_MAX]; // fixed job table, no alloc
    uint32_t count;                           // live jobs (0..JOBS_MAX)
} ProcessSpawn;

// Empty table, zero budget default.
ProcessSpawn *ProcessSpawn_0(void);

// Empty table with a default spawn budget in milliseconds.
ProcessSpawn *ProcessSpawn_1(uint64_t timeoutMs);

#define ProcessSpawn(...) CONSTRUCTOR_DISPATCH(ProcessSpawn, __VA_ARGS__)

// Release the table block (null-safe no-op; children are NOT reaped here —
// cancel + poll first under the Vertical Integration Law teardown contract).
void ProcessSpawn_free(ProcessSpawn *self);

// Launch argv (NULL-terminated, argv[0] = program) via posix_spawnp into
// the table (Semantic Consistency Law, Argument order). False on null args, full table, or
// spawn failure. Mirrors pid/timeoutMs on success.
bool ProcessSpawn_spawn(const char *const *argv, uint64_t timeoutMs,
                        ProcessSpawn *dest);

// Reap done children with non-blocking waitpid slices, at most budgetNs
// (clamped to PROCESS_SPAWN_POLL_MAX_NS), ~1ms apart, cancel-aware.
// Returns true when every tracked job is done.
bool ProcessSpawn_poll(ProcessSpawn *self, uint64_t budgetNs);

// Raise the cancel flag and SIGTERM every unfinished job (no SIGKILL loop,
// no blocking wait — reap via poll under the Bounded Wait Law).
void ProcessSpawn_cancel(ProcessSpawn *self);

// Symmetric accessors (Single Class Per File Law, symmetric-accessors clause).
void ProcessSpawn_setPid(ProcessSpawn *self, int32_t pid);
int32_t ProcessSpawn_getPid(const ProcessSpawn *self);
void ProcessSpawn_setTimeoutMs(ProcessSpawn *self, uint64_t timeoutMs);
uint64_t ProcessSpawn_getTimeoutMs(const ProcessSpawn *self);
void ProcessSpawn_setExitCode(ProcessSpawn *self, int32_t exitCode);
int32_t ProcessSpawn_getExitCode(const ProcessSpawn *self);
void ProcessSpawn_setCancelFlag(ProcessSpawn *self, bool cancelFlag);
bool ProcessSpawn_isCancelFlag(const ProcessSpawn *self);
uint32_t ProcessSpawn_getCount(const ProcessSpawn *self);
int32_t ProcessSpawn_getJobPid(const ProcessSpawn *self, uint32_t i);
int32_t ProcessSpawn_getJobExitCode(const ProcessSpawn *self, uint32_t i);
bool ProcessSpawn_isJobDone(const ProcessSpawn *self, uint32_t i);

#endif

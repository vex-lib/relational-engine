#ifndef THREAD_REGISTRY_H
#define THREAD_REGISTRY_H

#include <stdint.h>

// thread/registry.h — thread identity & roles (Legacy: thread/ThreadRegistry.java).
//
// Every thread that touches the engine registers itself once and receives a
// dense index [0..255]: a lock-free probe-and-CAS over a fixed table keyed by
// the OS thread id, exactly like legacy. The index is the key downstream
// subsystems use for per-thread slots (pools, rings, telemetry) — no maps,
// no locks, zero allocation after boot.
//
// On top of legacy, threads carry a ROLE so subsystems can recognize who is
// asking: Thread 0 (the AppKit pump), the engine Loop, draw/present workers,
// and everything a user spawns (THREAD_ROLE_USER).

// Role tags. 0 = untagged.
enum {
    THREAD_ROLE_NONE       = 0,
    THREAD_ROLE_MAIN       = 1, // Thread 0: owns AppKit / the OS event pump
    THREAD_ROLE_ENGINE     = 2, // the fixed-timestep engine Loop
    THREAD_ROLE_DRAW       = 3, // render loop owner
    THREAD_ROLE_PRESENT    = 4, // swapchain blit cadence owner
    THREAD_ROLE_NETWORKING = 5,
    THREAD_ROLE_SCRIPTING  = 6,
    THREAD_ROLE_CONSOLE    = 7,
    THREAD_ROLE_UI         = 8, // darling layout/draw when it lands
    THREAD_ROLE_USER       = 9, // user-generated threads
};

#define THREAD_REGISTRY_SIZE 256

// Get or register the calling thread's dense index. O(1) lock-free, safe to
// call from any number of threads concurrently.
int ThreadRegistry_index(void);

// Tag / read a role on an index. Roles are written by the thread itself
// right after registration; readers treat them as advisory hints.
void ThreadRegistry_setRole(int index, int role);
int ThreadRegistry_role(int index);

const char *ThreadRegistry_roleName(int role);

#endif

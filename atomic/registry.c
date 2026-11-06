// thread/registry.c — thread identity & roles (Legacy: thread/ThreadRegistry.java port).
//
// The table is the legacy design verbatim: 256 atomic slots keyed by OS
// thread id, linear probing with CAS on empty slots. A slot holding the
// caller's id means "already registered here". Ids come from pthread_self(),
// which is unique per live thread in this process — the C stand-in for the
// JVM's Thread.threadId().
//
// Roles ride beside the identity table, one int per index. They are written
// once by the owning thread and read by anyone; plain atomics keep it simple.

#include "atomic/registry.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "util/annotation.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Registry
 * ============================================================================
 * Process-wide thread identity and role table, the C stand-in for the JVM's
 * Thread.threadId(). 256 atomic slots are keyed by OS thread id (pthread_self)
 * with linear probing and CAS on empty slots, so any thread can register
 * itself once and read its dense index O(1) forever after. Roles ride beside
 * the identity table as one int per index, written once by the owning thread
 * and read by anyone through plain atomics. The table is fixed-size by design
 * (a process has a bounded live thread count) and never allocates; slot
 * indices never invalidate, so callers may cache them across calls.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Registry (atomic/registry.c)
 * ============================================================================
 * thread identity & roles (Legacy: thread/ThreadRegistry.java).
 *
 * STRUCT FIELDS: none — procedural (operates on thread registry table (dense index + roles))
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - ThreadRegistry_index(void)
 *   - ThreadRegistry_role(index)
 *   - ThreadRegistry_roleName(role)
 *
 * Setters:
 *   - ThreadRegistry_setRole(index, role)
 * ============================================================================
 */


static _Atomic uint64_t s_table[THREAD_REGISTRY_SIZE]; // 0 = empty slot
static _Atomic int32_t s_roles[THREAD_REGISTRY_SIZE];

int ThreadRegistry_index(void) {
    uint64_t tid = (uint64_t)pthread_self();
    int index = (int)(tid & (THREAD_REGISTRY_SIZE - 1));

    while (true) {
        uint64_t registered = atomic_load_explicit(&s_table[index], memory_order_acquire);

        // Hit: this thread already owns the slot.
        if (registered == tid)
            return index;

        // Empty: claim it with a CAS; losers just probe again.
        if (registered == 0) {
            uint64_t expected = 0;
            if (atomic_compare_exchange_strong(&s_table[index], &expected, tid))
                return index;
        }

        // Collision: next slot.
        index = (index + 1) & (THREAD_REGISTRY_SIZE - 1);
    }
}

void ThreadRegistry_setRole(int index, int role) {
    if (index < 0 || index >= THREAD_REGISTRY_SIZE) return;
    atomic_store_explicit(&s_roles[index], role, memory_order_release);
}

int ThreadRegistry_role(int index) {
    if (index < 0 || index >= THREAD_REGISTRY_SIZE) return THREAD_ROLE_NONE;
    return atomic_load_explicit(&s_roles[index], memory_order_acquire);
}

const char *ThreadRegistry_roleName(int role) {
    switch (role) {
        case THREAD_ROLE_MAIN:       return "_main";
        case THREAD_ROLE_ENGINE:     return "engine";
        case THREAD_ROLE_DRAW:       return "draw";
        case THREAD_ROLE_PRESENT:    return "present";
        case THREAD_ROLE_NETWORKING: return "networking";
        case THREAD_ROLE_SCRIPTING:  return "scripting";
        case THREAD_ROLE_CONSOLE:    return "console";
        case THREAD_ROLE_UI:         return "ui";
        case THREAD_ROLE_USER:       return "user";
        default:                     return "none";
    }
}

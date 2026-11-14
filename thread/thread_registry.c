// thread/registry.c — thread identity & opaque tags (Legacy: thread/ThreadRegistry.java port).
//
// The table is the legacy design verbatim: 256 atomic slots keyed by OS
// thread id, linear probing with CAS on empty slots. A slot holding the
// caller's id means "already registered here". Ids come from pthread_self(),
// which is unique per live thread in this process — the C stand-in for the
// JVM's Thread.threadId().
//
// A tag rides beside the identity table, one u32 per index. It is written
// once by the owning thread and read by anyone; plain atomics keep it simple.
// The tag's meaning is policy and belongs to the behavior owner, never here.

#include "vex.h"
#include "thread.h"

#include <pthread.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ThreadRegistry
 * ============================================================================
 * Process-wide thread identity table, the C stand-in for the JVM's
 * Thread.threadId(). 256 atomic slots are keyed by OS thread id (pthread_self)
 * with linear probing and CAS on empty slots, so any thread can register
 * itself once and read its dense index O(1) forever after. One opaque u32 tag
 * rides beside the identity table, written once by the owning thread and read
 * by anyone through plain atomics. The table is fixed-size by design (a
 * process has a bounded live thread count) and never allocates; slot indices
 * never invalidate, so callers may cache them across calls.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: ThreadRegistry (thread/registry.c)
 * ============================================================================
 * thread identity & opaque tags (Legacy: thread/ThreadRegistry.java).
 *
 * STRUCT FIELDS: none — procedural (operates on thread registry table (dense index + tags))
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - ThreadRegistry_index()
 *   - ThreadRegistry_tag(index)
 *
 * Setters:
 *   - ThreadRegistry_setTag(index, tag)
 * ============================================================================
 */


static _Atomic u64 s_table[THREAD_REGISTRY_DEFAULT_SIZE]; // 0 = empty slot
static _Atomic u32 s_tags[THREAD_REGISTRY_DEFAULT_SIZE];

int ThreadRegistry_index() {
    u64 tid = (u64)pthread_self();
    int index = (int)(tid & (THREAD_REGISTRY_DEFAULT_SIZE - 1));

    while (true) {
        u64 registered = atomic_load_explicit(&s_table[index], memory_order_acquire);

        if (registered == tid)
            return index;

        if (registered == 0) {
            u64 expected = 0;
            if (atomic_compare_exchange_strong(&s_table[index], &expected, tid))
                return index;
        }

        index = (index + 1) & (THREAD_REGISTRY_DEFAULT_SIZE - 1);
    }
}

fn ThreadRegistry_setTag(int index, u32 tag) {
    if (index < 0 || index >= THREAD_REGISTRY_DEFAULT_SIZE) return;
    atomic_store_explicit(&s_tags[index], tag, memory_order_release);
}

u32 ThreadRegistry_tag(int index) {
    if (index < 0 || index >= THREAD_REGISTRY_DEFAULT_SIZE) return THREAD_TAG_NONE;
    return atomic_load_explicit(&s_tags[index], memory_order_acquire);
}

#ifndef THREAD_REGISTRY_H
#define THREAD_REGISTRY_H

#include "vex.h"

#include "lang/primitive.h"

// thread/thread.h — process-wide thread identity and opaque tag table.
//
// Mechanism only: every thread registers itself once and receives a dense
// index in [0, THREAD_REGISTRY_DEFAULT_SIZE). Callers may attach one opaque
// u32 tag; what a tag means is policy and lives with the behavior owner
// (Vexspoke's ThreadRole). Indices are stable for the life of the process and
// the table never allocates.
//
// INTENTIONAL(vex): the table is fixed-capacity by design — a process has a
// bounded live thread count, linear probing needs a power-of-two mask, and
// registration must stay allocation-free. THREAD_REGISTRY_DEFAULT_SIZE is the
// shipped bound, not a tunable. Slot indices never invalidate.

#define THREAD_REGISTRY_DEFAULT_SIZE 256
#define THREAD_TAG_NONE 0u

int ThreadRegistry_index();

void ThreadRegistry_setTag(int index, u32 tag);
u32 ThreadRegistry_tag(int index);

#endif

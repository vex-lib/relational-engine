#include "vex.h"
#include "substrate/variable/var.h"

#include <stdio.h>
#include <string.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: var
 * ============================================================================
 * An arena-owned reactive binding. Carries one 32-byte name box (VariableSlot)
 * and a thin opaque value pointer. `var` owns the glue: a fixed subscriber
 * list. Subscribers are function pointers; nobody dereferences them, and the
 * var never allocates on notify beyond the value replacement.
 *
 * Lifetime: arena-allocated in the caller's arena (var_2) and released wholesale
 * at arena teardown. Every field is process-lifetime within that arena.
 *
 * Path temperature: cold. Reactive publish is a subscriber rendezvous. Never
 * per-frame work (the Cold-Only Reflection Law).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: var (substrate/variable/var.c)
 * ============================================================================
 * var_2            arena-owned binding from a folded name
 * var_set          publish one value to the subscriber list
 * var_subscribe    append one (observer, context) pair
 * var_unsubscribe  remove one matching pair
 * var_unsubscribeAll clear the list
 * var_name         borrowed name view, null-safe
 * var_value        borrowed value word, null-safe
 * var_observerCount live subscriber count, null-safe
 * var_arena        owning arena, null-safe
 * toString         value projection
 * toStringStruct   structure projection
 * ============================================================================
 */

var *var_2(const char *name, MemoryArena *arena) {
    if (!name || !arena)
        return nullptr;

    var *self = MemoryArena_alloc(arena, TYPE_VAR_SINGLETON, sizeof(var));
    if (!self)
        return nullptr;

    memset(self, 0, sizeof(*self));

    if (!VariableSlot_init(&(*self).slot, name, (pointer) self)) {
        MemoryArena_free(arena, self);
        return nullptr;
    }

    (*self).arena = arena;
    return self;
}

bool var_set(var *self, pointer value) {
    if (!self)
        return false;

    VariableSlot_setPointer(&(*self).slot, (pointer) self);
    (*self).value = value;

    for (bytes i = 0; i < (*self).observerCount; i++) {
        VarObserver observer = (*self).observers[i];
        if (observer)
            observer(self, value, (*self).observerContext[i]);
    }

    return true;
}

bool var_subscribe(var *self, VarObserver observer, any context) {
    if (!self || !observer)
        return false;
    if ((*self).observerCount >= VAR_MAX_OBSERVERS)
        return false;

    bytes index = (*self).observerCount;
    (*self).observers[index] = observer;
    (*self).observerContext[index] = context;
    (*self).observerCount += 1u;
    return true;
}

fn var_unsubscribe(var *self, VarObserver observer, any context) {
    if (!self || !observer)
        return;

    bytes write = 0u;
    for (bytes read = 0; read < (*self).observerCount; read++) {
        if ((*self).observers[read] == observer &&
            (*self).observerContext[read] == context)
            continue;
        (*self).observers[write] = (*self).observers[read];
        (*self).observerContext[write] = (*self).observerContext[read];
        write += 1u;
    }

    for (bytes i = write; i < VAR_MAX_OBSERVERS; i++) {
        (*self).observers[i] = nullptr;
        (*self).observerContext[i] = nullptr;
    }
    (*self).observerCount = write;
}

fn var_unsubscribeAll(var *self) {
    if (!self)
        return;

    memset((*self).observers, 0, sizeof((*self).observers));
    memset((*self).observerContext, 0, sizeof((*self).observerContext));
    (*self).observerCount = 0u;
}

const char *var_name(const var *self) {
    return self ? VariableSlot_getName(&(*self).slot) : nullptr;
}

pointer var_value(const var *self) {
    return self ? (*self).value : 0u;
}

bytes var_observerCount(const var *self) {
    return self ? (*self).observerCount : 0u;
}

MemoryArena *var_arena(const var *self) {
    return self ? (*self).arena : nullptr;
}

fn var_toString(const var *self, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!self) {
        snprintf(dest, cap, "nullptr");
        return;
    }

    int written = snprintf(dest, cap, "var(%s => 0x%llx, subscribers=%zu)",
                           VariableSlot_getName(&(*self).slot),
                           (unsigned long long) (*self).value,
                           (*self).observerCount);
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

fn var_toStringStruct(const var *self, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!self) {
        snprintf(dest, cap, "nullptr");
        return;
    }

    int written = snprintf(dest, cap, "var { name=\"%s\", value=0x%llx, subscribers=%zu }",
                           VariableSlot_getName(&(*self).slot),
                           (unsigned long long) (*self).value,
                           (*self).observerCount);
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

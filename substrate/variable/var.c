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

;;GETTER
static inline VariableSlot *Var_slotMut(var *self) { return &(*self).slot; }
;;GETTER
static inline const VariableSlot *Var_slot(const var *self) { return &(*self).slot; }
;;SETTER
static inline fn Var_setArena(var *self, MemoryArena *arena) { (*self).arena = arena; }
;;SETTER
static inline fn Var_setValue(var *self, pointer value) { (*self).value = value; }
;;GETTER
static inline VarObserver Var_observerAt(const var *self, bytes i) { return (*self).observers[i]; }
;;SETTER
static inline fn Var_setObserverAt(var *self, bytes i, VarObserver obs) { (*self).observers[i] = obs; }
;;GETTER
static inline any Var_observerContextAt(const var *self, bytes i) { return (*self).observerContext[i]; }
;;SETTER
static inline fn Var_setObserverContextAt(var *self, bytes i, any ctx) { (*self).observerContext[i] = ctx; }
;;GETTER
static inline VarObserver *Var_observersMut(var *self) { return (*self).observers; }
;;GETTER
static inline any *Var_observerContextMut(var *self) { return (*self).observerContext; }
;;SETTER
static inline fn Var_setObserverCount(var *self, bytes count) { (*self).observerCount = count; }
;;GETTER
static inline pointer Var_valueRaw(const var *self) { return (*self).value; }
;;GETTER
static inline bytes Var_observerCountRaw(const var *self) { return (*self).observerCount; }
;;GETTER
static inline MemoryArena *Var_arenaRaw(const var *self) { return (*self).arena; }

var *var_2(const char *name, MemoryArena *arena) {
    if (!name || !arena) 
        return nullptr;

    var *self = MemoryArena_alloc(arena, TYPE_VAR_SINGLETON, sizeof(var));

    if (!self) 
        return nullptr;

    memset(self, 0, sizeof(*self));

    if (!VariableSlot_init(Var_slotMut(self), name, (pointer) self)) {
        MemoryArena_free(arena, self);

        return nullptr;
    }

    Var_setArena(self, arena);

    return self;
}

bool var_set(var *self, pointer value) {
    if (!self) 
        return false;

    VariableSlot_setPointer(Var_slotMut(self), (pointer) self);

    Var_setValue(self, value);

    bytes count = var_observerCount(self);
    for (bytes i = 0; i < count; i++) {
        VarObserver observer = Var_observerAt(self, i);

        if (observer) 
            observer(self, value, Var_observerContextAt(self, i));
    }

    return true;
}

bool var_subscribe(var *self, VarObserver observer, any context) {
    if (!self || !observer) 
        return false;

    bytes index = var_observerCount(self);
    if (index >= VAR_MAX_OBSERVERS) 
        return false;

    Var_setObserverAt(self, index, observer);

    Var_setObserverContextAt(self, index, context);

    Var_setObserverCount(self, index + 1u);

    return true;
}

fn var_unsubscribe(var *self, VarObserver observer, any context) {
    if (!self || !observer) 
        return;

    bytes write = 0u;
    bytes count = var_observerCount(self);

    for (bytes read = 0; read < count; read++) {
        VarObserver obs = Var_observerAt(self, read);
        any ctx = Var_observerContextAt(self, read);
        if (obs == observer && ctx == context)
            continue;
        Var_setObserverAt(self, write, obs);

        Var_setObserverContextAt(self, write, ctx);

        write += 1u;
    }

    for (bytes i = write; i < VAR_MAX_OBSERVERS; i++) {
        Var_setObserverAt(self, i, nullptr);

        Var_setObserverContextAt(self, i, nullptr);
    }

    Var_setObserverCount(self, write);
}

fn var_unsubscribeAll(var *self) {
    if (!self) 
        return;

    memset(Var_observersMut(self), 0, sizeof(((var) {0}).observers));

    memset(Var_observerContextMut(self), 0, sizeof(((var) {0}).observerContext));

    Var_setObserverCount(self, 0u);
}

const char *var_name(const var *self) {
    return self ? VariableSlot_getName(Var_slot(self)) : nullptr;
}

pointer var_value(const var *self) {
    return self ? Var_valueRaw(self) : 0u;
}

bytes var_observerCount(const var *self) {
    return self ? Var_observerCountRaw(self) : 0u;
}

MemoryArena *var_arena(const var *self) {
    return self ? Var_arenaRaw(self) : nullptr;
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

    i32 written = snprintf(dest, cap, "var(%s => 0x%llx, subscribers=%zu)",
                           VariableSlot_getName(var_slot(self)),
                           (u64) var_value(self),
                           var_observerCount(self));
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

    i32 written = snprintf(dest, cap, "var { name=\"%s\", value=0x%llx, subscribers=%zu }",
                           VariableSlot_getName(var_slot(self)),
                           (u64) var_value(self),
                           var_observerCount(self));
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated) 
            *outTruncated = true;
    }
}

#ifndef RELATIONAL_VAR_H
#define RELATIONAL_VAR_H

#include "vex.h"

#include "nio/mem.h"
#include "substrate/variable/variable_slot.h"
#include "type/type.h"

// substrate/variable/var.h — an arena-owned reactive binding.
//
// The caller asks for `var *v = var("name", &arena);` and gets a stable pointer
// inside that arena. The name is the 32-byte VariableSlot atom; the value is a
// thin opaque pointer. `var` owns the reactive glue: subscribers are called
// with (self, value, context) when var_set publishes. The row lives or dies
// with the arena.
//
// INTENTIONAL(vex): `var` is lowercase on purpose — vocabulary, not a
// CapitalCase class, consistent with the floor's `string` spelling.
//
// This is deliberately NOT a Cell/VariableSlot merge: VariableSlot owns the
// name grammar, Cell owns the uniform value slot, var owns the glue between
// them. Cold path only — the rendezvous, never per-frame.

#define TYPE_VAR_SINGLETON Type_make(PROJ_RELATIONAL_ENGINE, FORM_SINGLETON, 12u)
#define VAR_MAX_OBSERVERS 8u

typedef struct var var;

typedef void (*VarObserver)(var *self, uintptr_t value, void *context);

;;CLASSNAME("var")
struct var {
    VariableSlot slot;
    uintptr_t value;
    MemoryArena *arena;
    VarObserver observers[VAR_MAX_OBSERVERS];
    void *observerContext[VAR_MAX_OBSERVERS];
    bytes observerCount;
};

var *var_2(const char *name, MemoryArena *arena);
#define var(...) var_2(__VA_ARGS__)

bool var_set(var *self, uintptr_t value);
bool var_subscribe(var *self, VarObserver observer, void *context);
void var_unsubscribe(var *self, VarObserver observer, void *context);
void var_unsubscribeAll(var *self);

const char *var_name(const var *self);
uintptr_t var_value(const var *self);
bytes var_observerCount(const var *self);
MemoryArena *var_arena(const var *self);

void var_toString(const var *self, char *dest, bytes cap, bool *outTruncated);
void var_toStringStruct(const var *self, char *dest, bytes cap, bool *outTruncated);

#endif

#ifndef RELATIONAL_ENGINE_VARIABLE_REGISTRY_H
#define RELATIONAL_ENGINE_VARIABLE_REGISTRY_H

#include "vex.h"

// One opaque owner plus its behaviorless borrowed row view. Rust owns all rows.
typedef struct ReVariableRegistry ReVariableRegistry;
typedef struct ReVariableSlot {
    u8 name[24];
    const u8 *pointer; // Borrowed value address, NOT the slot's own address.
} ReVariableSlot;
static_assert(sizeof(ReVariableSlot) == 32, "64-bit variable slot ABI");
static_assert(offsetof(ReVariableSlot, name) == 0, "name starts at zero");
static_assert(offsetof(ReVariableSlot, pointer) == 24, "value pointer follows name");

u32 re_variables_new(bytes rowsPerChunk, ReVariableRegistry **outOwner);
fn re_variables_drop(ReVariableRegistry *owner);
u32 re_variables_add(ReVariableRegistry *owner, const u8 *name,
    bytes nameBytes, const u8 *valuePointer, bytes *outIndex);
u32 re_variables_find(const ReVariableRegistry *owner, const u8 *name,
    bytes nameBytes, bytes *outIndex);
u32 re_variables_slot(const ReVariableRegistry *owner, bytes index,
    const ReVariableSlot **outSlot);
u32 re_variables_set_pointer(ReVariableRegistry *owner, bytes index,
    const u8 *valuePointer);
#endif

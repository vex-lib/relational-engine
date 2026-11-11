#ifndef RELATIONAL_VARIABLE_SLOT_H
#define RELATIONAL_VARIABLE_SLOT_H

#include "vex.h"

#include "lang/constructor.h"

// substrate/variable/variable_slot.h — the 32-byte name box (the variable slot atom).
//
// The Relational Engine's reflection atom: a 32-byte record holding a folded
// 23-character name (plus NUL, zero-padded to 24) and an 8-byte pointer. It is
// the LABEL on the jar — the name states the symbol, the pointer addresses its
// value cell on the global shelf (the reflection plan, section 3.7). The slot
// carries no value of its own.
//
// Per the 24-Byte Variable Slot Law (The 23+1 Rule): names are 1..23 ASCII
// characters folded to lowercase over the charset [a-z0-9_$-], with '.' as the
// SEARCH SPLITTER between segments (character.position.x); the dot is stored but
// is never a segment of its own. Two slots pack into one 64-byte cache line with
// zero padding waste.
//
// A slot is usable inline (a pool row) or arena-allocated (the arity
// constructors). All behavior takes the slot by pointer; getters are null-safe
// (the Symmetric Getter/Setter Completeness Law). Cold-path only — a name box
// is read by the reflective walk, which is a cold rendezvous (the Cold-Only
// Reflection Law).

#define VARIABLE_SLOT_NAME_MAX 23u
#define VARIABLE_SLOT_NAME_BYTES 24u
#define VARIABLE_SLOT_SIZE 32u

typedef struct VariableSlot {
    char name[VARIABLE_SLOT_NAME_BYTES];  // folded name, NUL-terminated, zero-padded
    uintptr_t pointer;                    // the value cell address (the destination)
} VariableSlot;

_Static_assert(sizeof(VariableSlot) == VARIABLE_SLOT_SIZE, "VariableSlot must stay 32 Bytes");

bool VariableSlot_init(VariableSlot *self, const char *name, uintptr_t pointer);

bool VariableSlot_foldName(const char *name, char *out);

int VariableSlot_bucketOf(char c);

VariableSlot *VariableSlot_0(void);
VariableSlot *VariableSlot_1(const char *name);
VariableSlot *VariableSlot_2(const char *name, uintptr_t pointer);

#define VariableSlot(...) CONSTRUCTOR_DISPATCH(VariableSlot, __VA_ARGS__)

void VariableSlot_free(VariableSlot *self);

bool VariableSlot_setName(VariableSlot *self, const char *name);
void VariableSlot_setPointer(VariableSlot *self, uintptr_t pointer);

const char *VariableSlot_getName(const VariableSlot *self);
uintptr_t VariableSlot_getPointer(const VariableSlot *self);
bool VariableSlot_isEmpty(const VariableSlot *self);
bool VariableSlot_nameEquals(const VariableSlot *self, const char *name);

void VariableSlot_toString(const VariableSlot *self, char *dest, bytes cap, bool *outTruncated);
void VariableSlot_toStringStruct(const VariableSlot *self, char *dest, bytes cap, bool *outTruncated);

#endif

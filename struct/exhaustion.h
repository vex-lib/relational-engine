#ifndef STRUCT_EXHAUSTION_H
#define STRUCT_EXHAUSTION_H



// struct/exhaustion.h — the shared loud-refusal seam (the Exhaustion Loudness
// Law) for owners that own finite storage but do not embed a Collection.
//
// An owner that owns finite storage and refuses a request routes the refusal
// through Struct_reportExhaustion: the caller-supplied count always increments,
// and the epoch's FIRST refusal emits one THROW naming the owner and the
// requested size. The owner starts a fresh epoch when it is created or reset
// (Struct_resetExhaustion). Collection embeds the same accounting and delegates
// here, so every finite-storage owner reports through one implementation.

// Count a refusal and report the epoch's first one. Always returns false, so a
// caller can write `return Struct_reportExhaustion(...)`. `boundBytes` is the
// capacity the owner hit (0 when there is no meaningful bound), so the
// diagnostic names the request AND the bound (the Exhaustion Loudness Law).
#include "vex.h"

;;VALIDATOR
bool Struct_reportExhaustion(u64 *count, bool *reported, const char *owner,
                             bytes requestedBytes, bytes boundBytes);

// The owner's refusal count for the current epoch.
;;TEST
u64 Struct_exhaustionCount(const u64 *count);

// Start a fresh epoch for the owner (count zeroed, diagnostic armed again).
;;TEST
fn Struct_resetExhaustion(u64 *count, bool *reported);

// --- Construction refusals (no owner instance exists yet) ---
// A constructor that cannot allocate its struct or backing returns nullptr; the
// refusal has no per-instance counter, so it is accounted process-wide: counted
// always, reported once per process epoch. Same loudness contract, one owner.

// Count a construction refusal and report the epoch's first one. Always false.
;;VALIDATOR
bool Struct_reportConstructionExhaustion(const char *owner, bytes requestedBytes);

// The process-wide construction-refusal count for the current epoch.
;;TEST
u64 Struct_constructionExhaustionCount();

// Start a fresh process-wide construction epoch.
;;TEST
fn Struct_resetConstructionExhaustion();

#endif

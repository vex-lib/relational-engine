#ifndef RELATIONAL_SYMBOL_TABLE_H
#define RELATIONAL_SYMBOL_TABLE_H

#include "vex.h"

#include "lang/constructor.h"

// substrate/relational/symbol_table.h — the relational symbol registry (Legacy: substrate/variable/SymbolTable.java).
//
// Maps a name to a (classId, targetPointer) payload. Every registered symbol
// is a row: the name lives in the shared string pool (referenced by slot,
// never copied), the pointer is the value. This is the heart of "everything
// is a pointer" — a name/pointer table whose values are themselves addresses
// of other headed blocks.
//
// Row layout (16 Bytes): [slot u32][classId u32][pointer u64]. Class is
// pinned at creation (no setter by design — rebind the value, not the kind).
// Lookup by name goes pool-first (binary search, shared globally), then one
// sparse hop (slot -> var id). Typed queries filter rows by classId
// (SymbolTable_findByClass) — one structure, no segregated lists to desync.
//
// Name policy (enforced cold, printed loud): ASCII alnum plus underscore
// and dot ([A-Za-z0-9_.], folded to lowercase), 1..23 chars. Anything else
// — empty, overlong, multibyte/UTF-16 garbage — is rejected. Mutations
// (instant, rename) print to stderr on rejection; lookups fail silent (-1),
// because speculative probing (spotlight) must never log.
//
// SLOT RECORD (owned by SymbolTable, behaviorless):
//   SymbolRow slot;     // i32 + pool slot index (name lives in pool)
//   SymbolRow classId;  // u32 + value class, pinned at creation
//   SymbolRow pointer;  // uintptr_t + the value (everything is a pointer)

#define SYMBOL_TABLE_DEFAULT_CAPACITY 1024

typedef struct SymbolRow {
    i32 slot;       // pool slot index (name lives in the pool)
    u32 classId;   // value class, pinned at creation
    uintptr_t pointer;  // the value (everything is a pointer)
} SymbolRow;

typedef struct SymbolTable {
    bool active;            // runtime-active flag
    SymbolRow *rows;      // dense rows, varId == index (append-only)
    u32 count;         // live rows
    u32 capacity;      // allocated rows
    i32 *bySlot;        // pool slot -> varId, -1 empty (sparse, grown)
    u32 bySlotCap;     // bySlot slots allocated
} SymbolTable;

SymbolTable SymbolTable_0(void);
#define SymbolTable(...) CONSTRUCTOR_DISPATCH(SymbolTable, __VA_ARGS__)

void SymbolTable_shutdown(SymbolTable *v);

i32 SymbolTable_instant(SymbolTable *v, const char *name, u32 classId, uintptr_t targetPointer);

bool SymbolTable_rename(SymbolTable *v, const char *oldName, const char *newName);

i32 SymbolTable_getId(SymbolTable *v, const char *name);

bytes SymbolTable_findByClass(SymbolTable *v, u32 classId, i32 *outIds, bytes cap);

uintptr_t SymbolTable_getPointer(SymbolTable *v, i32 varId);
void SymbolTable_setPointer(SymbolTable *v, i32 varId, uintptr_t targetPointer);
bool SymbolTable_compareAndSetPointer(SymbolTable *v, i32 varId, uintptr_t expected, uintptr_t newPointer);
u32 SymbolTable_getClassId(SymbolTable *v, i32 varId);

const char *SymbolTable_getName(const SymbolTable *v, i32 varId);

bytes SymbolTable_getActiveCount(SymbolTable *v);

#endif

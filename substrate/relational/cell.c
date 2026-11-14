// relational/cell.c — the 32-byte identity cell.
//
// Allocate an arena block whose 32-byte MemoryHeader is the identity; the
// payload is one Cell (value + pad). No behavior beyond the header reads and the
// value slot.

#include "vex.h"
#include "substrate/relational/cell.h"

#include <stdio.h>

#include "lang/annotation.h"
#include "nio/mem.h"

_Static_assert(CELL_BLOCK_BYTES == sizeof(MemoryHeader) + CELL_PAYLOAD_BYTES,
    "Cell block is the 32-byte identity header plus the 16-byte payload");

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Cell
 * ============================================================================
 * The reflection engine's identity atom: one 48-byte arena block made of the
 * standard 32-byte MemoryHeader (id, hash, length, pointer — the identity) plus
 * a 16-byte payload (a value/pointer slot and padding). A cell is therefore
 * self-describing: Cell_check(cell, typeId) answers its kind straight from the
 * header, with no side table and no extra Bytes.
 *
 * Lifetime: arena-allocated (TYPE from the caller's typeId) and released with
 * Cell_free, or wholesale at arena teardown. The identity typeId is the
 * caller's — a cell is a shape, not a fixed class.
 *
 * Path temperature: cold. Cells are read by the reflective rendezvous, never on
 * a frame (the Cold-Only Reflection Law). All getters are null-safe.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Cell (relational/cell.c)
 * ============================================================================
 * the identity cell (32-byte MemoryHeader identity + a 16-byte value slot).
 *
 * STRUCT FIELDS (Mirroring relational/cell.h — the payload; the identity is the
 * block header before it):
 * ----------------------------------------------------------------------------
 *   Cell {
 *     pointer value; // thin pointer to the value, or the value inline
 *     pointer pad;   // tail padding so the block is 32 Bytes
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure data, no behavior): none.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Cell_0()                    : anonymous cell (typeId 0)
 *   - Cell_1(typeId)              : identity cell, zero value
 *   - Cell_2(typeId, value)       : identity cell, bound value
 *   - Cell_free(cell)             : release an arena cell
 *
 * Public Core Functions: (.h)
 *   - Cell_typeId(cell)           : the identity typeId
 *   - Cell_check(cell, typeId)    : identity compare (null-safe)
 *
 * Public Setters: (.h)
 *   - Cell_setValue(cell, value)
 *
 * Public Getters: (.h)
 *   - Cell_getValue(cell)
 *
 * Public String Projections: (.h)
 *   - Cell_toString(cell, dest, cap, outTruncated)
 *   - Cell_toStringStruct(cell, dest, cap, outTruncated)
 * ============================================================================
 */

// CONSTRUCTORS

Cell *Cell_2(u64 typeId, pointer value) {
    Cell *cell = Memory_alloc(typeId, CELL_PAYLOAD_BYTES);
    if (!cell)
        return nullptr;
    (*cell).value = value;
    (*cell).pad = 0u;
    return cell;
}

Cell *Cell_0() {
    return Cell_2(0u, 0u);
}

Cell *Cell_1(u64 typeId) {
    return Cell_2(typeId, 0u);
}

fn Cell_free(Cell *cell) {
    if (cell)
        Memory_free(cell);
}

// CORE FUNCTIONS

u64 Cell_typeId(const Cell *cell) {
    if (!cell)
        return 0u;
    return Memory_type((any) cell);
}

bool Cell_check(const Cell *cell, u64 typeId) {
    if (!cell)
        return false;
    return Memory_type((any) cell) == typeId;
}

// SETTERS

fn Cell_setValue(Cell *cell, pointer value) {
    if (!cell)
        return;
    (*cell).value = value;
}

// GETTERS

pointer Cell_getValue(const Cell *cell) {
    if (!cell)
        return 0u;
    return (*cell).value;
}

// STRING PROJECTIONS (the toString Law)

fn Cell_toString(const Cell *cell, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!cell) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Cell(typeId=0x%llx, value=0x%llx)",
                           (unsigned long long) Cell_typeId(cell),
                           (unsigned long long) (*cell).value);
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

fn Cell_toStringStruct(const Cell *cell, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!cell) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Cell { value=0x%llx, pad=0x%llx }",
                           (unsigned long long) (*cell).value,
                           (unsigned long long) (*cell).pad);
    if (written < 0 || (bytes) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

#ifndef RELATIONAL_CELL_H
#define RELATIONAL_CELL_H

#include "vex.h"
#include "lang/constructor.h"

// relational/cell.h — the identity cell.
//
// A cell is one arena block whose 32-byte MemoryHeader IS its identity — the
// "self-describing header" (the Self-Describing memory Block Law) made the unit
// of reflection. The block is:
//
//     [ MemoryHeader 32B: id | hash | length | pointer ][ value 8B ][ pad 8B ] = 48B
//       `----------------- the identity ---------------'  `-- the Cell payload --'
//
// The `value` field is a thin pointer to the cell's value, or the value inline
// for small kinds — one 8-byte slot either way, so the cell is uniform. The pad
// rounds the payload to 16 Bytes; the block is the 32-byte header plus that
// payload, and its payload is 32-byte aligned (the Universal Alignment Law).
//
// Cell_check(cell, typeId) reads the identity header's typeId, so a live
// pointer can answer "are you kind X?" without a side table. This is the block
// reactive objects are built on (the same header, a different payload shape).

#define CELL_PAYLOAD_BYTES 16u
// Whole block: the 32-byte identity header plus the 16-byte payload.
#define CELL_BLOCK_BYTES 48u

typedef struct Cell {
    pointer value; // thin pointer to the value, or the value inline
    pointer pad;   // tail padding so the block is 32 Bytes
} Cell;

_Static_assert(sizeof(Cell) == CELL_PAYLOAD_BYTES, "Cell payload must stay 16 Bytes");

Cell *Cell_0();
Cell *Cell_1(u64 typeId);
Cell *Cell_2(u64 typeId, pointer value);

#define Cell(...) CONSTRUCTOR_DISPATCH(Cell, __VA_ARGS__)

fn Cell_free(Cell *cell);

u64 Cell_typeId(const Cell *cell);
bool Cell_check(const Cell *cell, u64 typeId);

fn Cell_setValue(Cell *cell, pointer value);
pointer Cell_getValue(const Cell *cell);

fn Cell_toString(const Cell *cell, char *dest, bytes cap, bool *outTruncated);
fn Cell_toStringStruct(const Cell *cell, char *dest, bytes cap, bool *outTruncated);

#endif

#ifndef NIO_RELATIONAL_ROWS_H
#define NIO_RELATIONAL_ROWS_H
/* Engine-owned C client seam to Rust byte-row storage, separate from Memory_*.
 * Supply engine src + rust/include paths and link the resident Rust staticlib.
 * R1 owns the pool, excludes borrowers before mutation/drop, and keeps engine
 * code resident across consumer reloads. API/limits: relational_engine/row_pool.h. */
#include "relational_engine/row_pool.h"
#endif

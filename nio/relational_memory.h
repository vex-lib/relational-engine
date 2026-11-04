#ifndef NIO_RELATIONAL_MEMORY_H
#define NIO_RELATIONAL_MEMORY_H
/* Engine-owned byte/string handshake, separate from the Memory_* arena API.
 * Supply relational-engine/rust/include and link its resident static library.
 * Rust owns atomic storage; never reinterpret it as C _Atomic objects.
 * R1 keeps the engine and owners alive across consumer hot reloads. Quiesce
 * users before owner destruction. Record schema migration is not implemented.
 * Production build wiring supplies this engine boundary explicitly. */
#include "relational_engine/memory.h"
#endif

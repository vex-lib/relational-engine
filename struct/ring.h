#ifndef THREAD_RING_H
#define THREAD_RING_H

#include "vex.h"
#include "atomic/atomic.h"

/**
 * thread/ring.h, MPMC ring buffer API (Legacy: thread/RingBuffer.java).
 *
 * A fixed-capacity FIFO whose capacity is rounded to a power of two, so slot
 * indexing is `idx & mask` (no modulo). One embedded spinlock serializes
 * push/pop; head/tail are atomic counters. Producers push, one or more
 * consumers pop, all at zero allocation.
 */

/**
 * SpinLock lock      - serializes the actual copy in/_out
 * bytes capacity     - power of two
 * bytes mask         - capacity - 1, for slot masking
 * bytes elem_size    - Bytes per element
 * _Atomic bytes head - consumer index (monotonic, never wraps back)
 * _Atomic bytes tail - producer index (monotonic, never wraps back)
 * u8 *slots          - capacity * elem_size arena
 */
typedef struct RingBuffer {
    SpinLock lock;
    bytes capacity;
    bytes mask;
    bytes elem_size;
    _Atomic bytes head;
    _Atomic bytes tail;
    u8 *slots;
} RingBuffer;

/**
 * Construct a ring for elem_size-byte elements, capacity slots (rounded up to
 * pow2). A null slots pointer means invalid arguments or allocation failure.
 */
RingBuffer RingBuffer_0();
RingBuffer RingBuffer_2(bytes elem_size, bytes capacity);
#define RingBuffer(...) CONSTRUCTOR_DISPATCH(RingBuffer, __VA_ARGS__)

fn RingBuffer_shutdown(RingBuffer *ring);

/** Copy item into the ring. Returns false if full (caller retries/spins). */
bool RingBuffer_push(RingBuffer *ring, const_any item);

/** Copy oldest item _out into _out. Returns false if empty. */
bool RingBuffer_pop(RingBuffer *ring, any out);

#endif

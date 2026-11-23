/**
 * ring.c, MPMC ring buffer (thread/RingBuffer.java port).
 *
 * A fixed-capacity FIFO with power-of-two capacity so the slot index is a
 * cheap mask instead of a modulo. One spinlock guards the whole structure
 * (the legacy used the same idea: "spinlock-coordinated"). head/tail are
 * atomic bytes counters; the lock serializes the actual push/pop so the
 * reader never sees torn data.
 *
 * Full/empty are detected by head==tail (empty) vs. tail-head==capacity (full),
 * which is why capacity is *at least* as large as any burst you push.
 * Lesson 18 replaces the spinlock with per-thread slots later; this is the
 * correct, simple version that works today.
 */

#include "vex.h"
#include "atomic/atomic.h"
#include "nio/mem.h"
#include "type/re_type.h"

#include "struct/ring.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Ring
 * ============================================================================
 * Fixed-capacity MPMC FIFO with power-of-two capacity so the slot index is a
 * cheap mask instead of a modulo. One spinlock serializes the actual copy
 * in/out while head/tail are monotonic atomic counters, so readers never see
 * torn data and full/empty are exact (head==tail empty, tail-head==capacity
 * full). The slots arena is calloc'd once at construction and freed at shutdown,
 * push/pop allocate nothing on the hot path. Capacity is a hard contract:
 * any burst larger than the ring is rejected, never silently dropped.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Ring (atomic/ring.c)
 * ============================================================================
 * MPMC ring buffer API (Legacy: thread/RingBuffer.java).
 *
 * STRUCT FIELDS (Mirroring atomic/ring.h):
 * ----------------------------------------------------------------------------
 *   RingBuffer {
 *     SpinLock lock; // serializes the actual copy in/_out
 *     bytes capacity; // power of two
 *     bytes mask; // capacity - 1, for slot masking
 *     bytes elem_size; // Bytes per element
 *     _Atomic bytes head; // consumer index (monotonic, never wraps back)
 *     _Atomic bytes tail; // producer index (monotonic, never wraps back)
 *     u8 *slots; // capacity * elem_size arena
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - RingBuffer_0()
 *   - RingBuffer_2(elem_size, capacity)
 *
 * Core Functions:
 *   - RingBuffer_shutdown(ring)
 *   - RingBuffer_push(ring, item)
 *   - RingBuffer_pop(ring, out)
 * ============================================================================
 */


/**
 * Round up to the next power of two (clamp: 1..2^64). Classic bit trick,
 * same nextPowerOfTwo the Java version had.
 */
static bytes nextPow2(bytes value) {
    if (value <= 0) 
       return 1;

    value--;

    value |= value >> 1;

    value |= value >> 2;

    value |= value >> 4;

    value |= value >> 8;

    value |= value >> 16;

    value |= value >> 32;

    return value + 1;
}

/** Constructs the ring and allocates zeroed storage with rounded-up capacity. */
RingBuffer RingBuffer_0() {
    RingBuffer ring;

    memset(&ring, 0, sizeof ring);

    ring.lock = SPIN_LOCK_INIT;

    atomic_init(&ring.head, 0u);

    atomic_init(&ring.tail, 0u);

    return ring;
}

RingBuffer RingBuffer_2(bytes elem_size, bytes capacity) {
    RingBuffer ring = RingBuffer_0();

    if (elem_size == 0) 
       return ring;

    bytes cap = nextPow2(capacity);

    bytes total = cap * elem_size;

    if (total / elem_size != cap) {
        memset(&ring, 0, sizeof ring);

        return ring;
    }

    ring.capacity = cap;

    ring.mask = cap - 1;

    ring.elem_size = elem_size;

    ring.slots = (u8*) Memory_alloc(TYPE_RING_BUFFER, Re_arena(), total);

    if (ring.slots == nullptr) 
        memset(&ring, 0, sizeof ring);

    return ring;
}

;;GETTER
static inline u8 *RingBuffer_slots(const RingBuffer *ring)
{
    return (*ring).slots;
}
;;SETTER
static inline fn RingBuffer_setSlots(RingBuffer *ring, u8 *slots)
{
    (*ring).slots = slots;
}
;;GETTER
static inline SpinLock *RingBuffer_lockMut(RingBuffer *ring)
{
    return &(*ring).lock;
}
;;GETTER
static inline bytes RingBuffer_tailLoad(const RingBuffer *ring) {
    return atomic_load_explicit(&(*ring).tail, memory_order_relaxed);
}
;;GETTER
static inline fn RingBuffer_storeTail(RingBuffer *ring, bytes v) {
    atomic_store_explicit(&(*ring).tail, v, memory_order_release);
}
;;GETTER
static inline bytes RingBuffer_headLoad(const RingBuffer *ring) {
    return atomic_load_explicit(&(*ring).head, memory_order_relaxed);
}
;;GETTER
static inline fn RingBuffer_storeHead(RingBuffer *ring, bytes v) {
    atomic_store_explicit(&(*ring).head, v, memory_order_release);
}
;;GETTER
static inline bytes RingBuffer_capacity(const RingBuffer *ring)
{
    return (*ring).capacity;
}
;;GETTER
static inline bytes RingBuffer_mask(const RingBuffer *ring)
{
    return (*ring).mask;
}
;;GETTER
static inline bytes RingBuffer_elemSize(const RingBuffer *ring)
{
    return (*ring).elem_size;
}

/** Frees ring storage and clears its storage pointer. */
fn RingBuffer_shutdown(RingBuffer *ring) {
    if (ring && RingBuffer_slots(ring)) {
        Memory_free(RingBuffer_slots(ring));

        RingBuffer_setSlots(ring, nullptr);
    }
}

/** Pushes one item under the ring lock; returns false for invalid input or a full ring. */
bool RingBuffer_push(RingBuffer *ring, const_any item) {
    if (!ring || !item) 
       return false;

    SpinLock *lock = RingBuffer_lockMut(ring);
    SpinLock_lock(lock);

    bytes tail = RingBuffer_tailLoad(ring);

    bytes head = RingBuffer_headLoad(ring);

    if (tail - head >= RingBuffer_capacity(ring)) {
        SpinLock_unlock(lock);

        return false;
    }

    bytes slot = tail & RingBuffer_mask(ring);
    bytes elemSize = RingBuffer_elemSize(ring);

    memcpy(RingBuffer_slots(ring) + slot * elemSize, item, elemSize);

    RingBuffer_storeTail(ring, tail + 1);

    SpinLock_unlock(lock);

    return true;
}

/** Removes the oldest item under the ring lock; returns false for invalid input or empty ring. */
bool RingBuffer_pop(RingBuffer *ring, any out) {
    if (!ring || !out) 
       return false;

    SpinLock *lock = RingBuffer_lockMut(ring);
    SpinLock_lock(lock);

    bytes head = RingBuffer_headLoad(ring);

    bytes tail = RingBuffer_tailLoad(ring);

    if (head == tail) {
        SpinLock_unlock(lock);

        return false;
    }

    bytes slot = head & RingBuffer_mask(ring);
    bytes elemSize = RingBuffer_elemSize(ring);

    memcpy(out, RingBuffer_slots(ring) + slot * elemSize, elemSize);

    RingBuffer_storeHead(ring, head + 1);

    SpinLock_unlock(lock);

    return true;
}

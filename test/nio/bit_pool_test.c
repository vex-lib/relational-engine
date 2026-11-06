// re/test/nio/bit_pool_test.c — the BitPool class _test.
//
// Owner: Relational Engine (re/nio/bit_pool.c). Proves the engine pool
// contract that replaced vexspoke bit/bit.c:
//
//   - GEOMETRY: the empty constructor and invalid geometry (0 element width,
//     0 capacity) are not live, and allocating from them is a nullptr refusal;
//   - ALIGNMENT FLOOR: every payload is 32-byte aligned, for a narrow and for
//     a wide element width, because the slot header is exactly one floor unit;
//   - STAMPING: typeId and length round trip through the slot header;
//   - GROWTH: a pool built for 4 slots keeps serving past capacity by
//     appending a chunk, and live payloads neither move nor lose their bytes;
//   - RECYCLE: a released slot is the next one handed out;
//   - RELEASE GUARDS: a double release and a foreign pointer are refused
//     without corrupting the freelist;
//   - CONTAINMENT: contains/type/length reject null and foreign pointers;
//   - SHUTDOWN: idempotent, leaves the pool not live, and stops allocation.
//
// Deterministic and headless; the only allocation path is the engine arena.

#include <stdint.h>
#include <stdio.h>

#include "re/nio/bit_pool.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

// Exercises construction, allocation, growth, recycling, validation, shutdown.
int main(void) {
    // Lazy default arena: the pool anchors on it without an explicit init.
    CHECK(Memory_init(0));

    // --- Geometry: empty and invalid constructions own no backing.
    BitPool empty = BitPool();

    CHECK(!BitPool_live(&empty));

    CHECK(BitPool_alloc(&empty, 0x5150u) == nullptr);

    BitPool badElement = BitPool(0u, 4u);

    CHECK(!BitPool_live(&badElement));

    BitPool badCapacity = BitPool(8u, 0u);

    CHECK(!BitPool_live(&badCapacity));

    // --- Construction: 4 slots of 8-byte payloads on the default arena.
    BitPool pool = BitPool(8u, 4u);

    CHECK(BitPool_live(&pool));

    void *slots[4];

    for (int i = 0; i < 4; i++) {
        slots[i] = BitPool_alloc(&pool, 0x5150u);

        CHECK(slots[i] != nullptr);

        CHECK(((uintptr_t) slots[i] & (MEMORY_PAYLOAD_ALIGN - 1u)) == 0);

        CHECK(BitPool_contains(&pool, slots[i]));

        CHECK(BitPool_type(&pool, slots[i]) == 0x5150u);

        CHECK(BitPool_length(&pool, slots[i]) == 8u);
        *(uint64_t*) slots[i] = (uint64_t) (0x1000 + i);
    }

    // --- Growth: capacity 4 is a starting point, not a ceiling.
    void *fifth = BitPool_alloc(&pool, 0x5150u);

    CHECK(fifth != nullptr);

    CHECK(((uintptr_t) fifth & (MEMORY_PAYLOAD_ALIGN - 1u)) == 0);

    CHECK(pool.totalSlots >= 8u);

    // Live payloads never move and never lose their bytes on growth.
    for (int i = 0; i < 4; i++) {
        CHECK(BitPool_contains(&pool, slots[i]));

        CHECK(*(uint64_t*) slots[i] == (uint64_t) (0x1000 + i));
    }

    // --- Recycle: a released slot is the next one handed out.
    BitPool_free(&pool, slots[2]);

    void *recycled = BitPool_alloc(&pool, 0x9999u);

    CHECK(recycled == slots[2]);

    CHECK(BitPool_type(&pool, recycled) == 0x9999u);

    CHECK(BitPool_length(&pool, recycled) == 8u);

    // --- Release guards: a double release must not create a freelist cycle.
    BitPool_free(&pool, recycled);

    BitPool_free(&pool, recycled);

    void *first = BitPool_alloc(&pool, 0xAAAAu);

    void *second = BitPool_alloc(&pool, 0xBBBBu);

    CHECK(first != nullptr);

    CHECK(second != nullptr);

    CHECK(first != second);

    CHECK(BitPool_type(&pool, first) == 0xAAAAu);

    CHECK(BitPool_type(&pool, second) == 0xBBBBu);

    // A released slot keeps its address in range but loses its stamp.
    BitPool_free(&pool, second);

    CHECK(BitPool_type(&pool, second) == 0u);

    // --- Foreign and null pointers are refused by every entry point.
    BitPool_free(&pool, nullptr);

    BitPool_free(&pool, (void*) (uintptr_t) 0x1);

    CHECK(!BitPool_contains(nullptr, second));

    CHECK(!BitPool_contains(&pool, nullptr));

    CHECK(!BitPool_contains(&pool, (void*) (uintptr_t) 0x1));

    CHECK(BitPool_type(&pool, (void*) (uintptr_t) 0x1) == 0u);

    CHECK(BitPool_length(&pool, (void*) (uintptr_t) 0x1) == 0u);

    // --- Alignment holds for a wide element too (24B -> stride rounds to 64).
    BitPool wide = BitPool(24u, 2u);

    CHECK(BitPool_live(&wide));

    for (int i = 0; i < 3; i++) {
        void *payload = BitPool_alloc(&wide, 0x7777u);

        CHECK(payload != nullptr);

        CHECK(((uintptr_t) payload & (MEMORY_PAYLOAD_ALIGN - 1u)) == 0);

        CHECK(BitPool_length(&wide, payload) == 24u);
    }

    BitPool_shutdown(&wide);

    CHECK(!BitPool_live(&wide));

    CHECK(BitPool_alloc(&wide, 0x7777u) == nullptr);

    // --- Shutdown is idempotent and stops allocation.
    BitPool_shutdown(&pool);

    CHECK(!BitPool_live(&pool));

    CHECK(BitPool_alloc(&pool, 0x5150u) == nullptr);

    BitPool_shutdown(&pool);

    CHECK(!BitPool_live(&pool));

    if (g_failures == 0) {
        printf("bit_pool_test: all assertions held\n");

        return 0;
    }

    printf("bit_pool_test: %d FAILURES\n", g_failures);

    return 1;
}

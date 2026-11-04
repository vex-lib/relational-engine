/* Real C client of Rust-owned aligned byte rows. Covers every extern, constructor
 * macro, output preservation, hostile geometry/identity/capacity, copied admission,
 * growth and reclaim stability, a named-registry binding, and reverse borrower /
 * storage teardown. No unsafe stale-pointer dereferences or concurrent mutation.
 * This is a consumer seam, NOT proof of any R3-R5 application or live Hot loader. */
#include "nio/relational_rows.h"
#include "relational_engine/variable_registry.h"
#include <assert.h>
#include <stdalign.h>
#include <string.h>
#include <stdint.h>

/* Plain C record stored as initialized bytes, never as a Rust object. */
typedef struct ExampleRow {
    uint64_t value;
    uint64_t revision;
} ExampleRow;

/* Proves the C handle matches Rust's target ABI and cannot hide uninitialized padding. */
static void layout(void) {
    _Static_assert(sizeof(ReRowHandle) == 24, "64-bit handle ABI");
    _Static_assert(offsetof(ReRowHandle, owner) == 0, "owner offset");
    _Static_assert(offsetof(ReRowHandle, index) == 8, "index offset");
    _Static_assert(offsetof(ReRowHandle, generation) == 16, "generation offset");
    _Static_assert(offsetof(ReRowHandle, reserved) == 20, "reserved offset");
}

/* Rejects geometry before publication; retains output owner on failure. */
static void geometry_rejections(void) {
    ReRowPool *pool = nullptr;
    assert(re_rows_new(0, 8, 1, &pool) == 1 && pool == nullptr);
    assert(re_rows_new(8, 0, 1, &pool) == 1 && pool == nullptr);
    assert(re_rows_new(8, 3, 1, &pool) == 1 && pool == nullptr);
    assert(re_rows_new(8, 8, 0, &pool) == 1 && pool == nullptr);
    assert(re_rows_new(SIZE_MAX, 8, 1, &pool) == 1 && pool == nullptr);
    assert(re_rows_new(8, 8, SIZE_MAX, &pool) == 1 && pool == nullptr);
    assert(re_rows_new(8, 8, 1, nullptr) == 1);
    assert(ReRowPool_3(8, 8, &pool) == 0);
    size_t size = 0, alignment = 0, capacity = 0;
    assert(re_rows_geometry(pool, &size, &alignment, &capacity) == 0);
    assert(size == 8 && alignment == 8 && capacity == RE_ROWS_PER_CHUNK_DEFAULT);
    re_rows_drop(pool);
    pool = nullptr;
    assert(ReRowPool(8, 8, &pool) == 0);
    re_rows_drop(pool);
    pool = nullptr;
    assert(ReRowPool_4(8, 8, 1, &pool) == 0);
    re_rows_drop(pool);
    re_rows_drop(nullptr);
}

/* Exercises copied rows, all null/output rejection paths, wrong owners and stable pointers. */
static void storage_contract(void) {
    ReRowPool *pool = nullptr;
    ReRowPool *other = nullptr;
    assert(ReRowPool(sizeof(ExampleRow), 256, 2, &pool) == 0);
    assert(ReRowPool(sizeof(ExampleRow), 256, 2, &other) == 0);
    ExampleRow input = {42, 1}, output = {99, 99};
    ReRowHandle first = ReRowHandle_zero(), foreign = ReRowHandle_zero();
    assert(re_rows_add(pool, (const uint8_t*) &input, sizeof(input), &first) == 0);
    assert(first.owner != 0 && first.generation != 0 && first.reserved == 0);
    assert(re_rows_add(other, (const uint8_t*) &input, sizeof(input), &foreign) == 0);
    input.value = 7; /* Original source is not retained. */
    bool cut = true;
    assert(re_rows_read(pool, first, (uint8_t*) &output, sizeof(output), &cut) == 0);
    assert(!cut && output.value == 42 && output.revision == 1);
    const uint8_t *borrowed = nullptr;
    assert(re_rows_borrow(pool, first, &borrowed) == 0);
    assert((uintptr_t) borrowed % 256 == 0);
    const uint8_t *original = borrowed;
    ReRowHandle saved = first;
    assert(re_rows_add(nullptr, (const uint8_t*) &input, sizeof(input), &saved) == 1);
    assert(re_rows_add(pool, nullptr, sizeof(input), &saved) == 1);
    assert(re_rows_add(pool, (const uint8_t*) &input, SIZE_MAX, &saved) == 1);
    assert(re_rows_add(pool, (const uint8_t*) &input, 0, &saved) == 1);
    assert(re_rows_add(pool, (const uint8_t*) &input, sizeof(input), nullptr) == 1);
    assert(memcmp(&saved, &first, sizeof(first)) == 0);
    assert(re_rows_read(nullptr, first, (uint8_t*) &output, sizeof(output), &cut) == 1);
    assert(re_rows_read(pool, first, nullptr, sizeof(output), &cut) == 1);
    assert(re_rows_read(pool, first, (uint8_t*) &output, SIZE_MAX, &cut) == 1);
    assert(re_rows_read(pool, first, (uint8_t*) &output, sizeof(output), nullptr) == 1);
    output.value = 99;
    cut = false;
    assert(re_rows_read(pool, first, (uint8_t*) &output, sizeof(output) - 1, &cut) == 4);
    assert(cut && output.value == 99);
    assert(re_rows_read(pool, first, nullptr, 0, &cut) == 4 && cut);
    assert(re_rows_read(pool, foreign, (uint8_t*) &output, sizeof(output), &cut) == 3);
    assert(output.value == 99);
    ReRowHandle bad = first;
    bad.reserved = 1;
    assert(re_rows_remove(pool, bad) == 3);
    bad = first;
    bad.index = SIZE_MAX;
    assert(re_rows_borrow(pool, bad, &borrowed) == 3 && borrowed == original);
    bad = ReRowHandle_zero();
    assert(re_rows_remove(pool, bad) == 3);
    assert(re_rows_remove(nullptr, first) == 1);
    assert(re_rows_write(nullptr, first, (const uint8_t*) &input, sizeof(input)) == 1);
    assert(re_rows_write(pool, first, nullptr, sizeof(input)) == 1);
    assert(re_rows_write(pool, first, (const uint8_t*) &input, sizeof(input) - 1) == 1);
    assert(re_rows_write(pool, foreign, (const uint8_t*) &input, sizeof(input)) == 3);
    assert(re_rows_write(pool, first, (const uint8_t*) &input, sizeof(input)) == 0);
    assert(re_rows_borrow(nullptr, first, &borrowed) == 1);
    assert(re_rows_borrow(pool, first, nullptr) == 1);
    size_t count = 99, released = 99, size = 99, alignment = 99, capacity = 99;
    assert(re_rows_len(nullptr, &count) == 1 && count == 99);
    assert(re_rows_len(pool, nullptr) == 1);
    assert(re_rows_geometry(nullptr, &size, &alignment, &capacity) == 1 && size == 99);
    assert(re_rows_geometry(pool, nullptr, &alignment, &capacity) == 1);
    assert(re_rows_geometry(pool, &size, nullptr, &capacity) == 1);
    assert(re_rows_geometry(pool, &size, &alignment, nullptr) == 1);
    assert(re_rows_release_empty(nullptr, &released) == 1 && released == 99);
    assert(re_rows_release_empty(pool, nullptr) == 1);
    assert(re_rows_geometry(pool, &size, &alignment, &capacity) == 0);
    assert(size == sizeof(input) && alignment == 256 && capacity == 2);
    ReRowHandle handles[1025]; /* Named test workload, not a production capacity ceiling. */
    for (size_t i = 0; i < 1025; ++i)
        assert(re_rows_add(pool, (const uint8_t*) &input, sizeof(input), &handles[i]) == 0);
    assert(re_rows_borrow(pool, first, &borrowed) == 0 && borrowed == original);
    assert(re_rows_len(pool, &count) == 0 && count == 1026);
    assert(re_rows_release_empty(pool, &released) == 0 && released == 0);
    char text[300];
    assert(re_rows_to_string(pool, text, sizeof(text), &cut) == 0 && !cut);
    assert(re_rows_to_string_struct(pool, text, sizeof(text), &cut) == 0 && !cut);
    assert(re_rows_to_string(nullptr, text, sizeof(text), &cut) == 0 && strcmp(text, "nullptr") == 0);
    assert(re_rows_to_string_struct(nullptr, text, sizeof(text), &cut) == 0);
    assert(re_rows_to_string(pool, nullptr, 0, &cut) == 4 && cut);
    assert(re_rows_to_string_struct(pool, text, 1, &cut) == 4 && cut);
    assert(re_rows_to_string(pool, nullptr, 1, &cut) == 1);
    assert(re_rows_to_string_struct(pool, nullptr, 1, &cut) == 1);
    assert(re_rows_to_string(pool, text, SIZE_MAX, &cut) == 1);
    assert(re_rows_to_string_struct(pool, text, SIZE_MAX, &cut) == 1);
    assert(re_rows_to_string(pool, text, sizeof(text), nullptr) == 1);
    assert(re_rows_to_string_struct(pool, text, sizeof(text), nullptr) == 1);
    for (size_t i = 0; i < 1025; ++i)
        assert(re_rows_remove(pool, handles[i]) == 0);
    assert(re_rows_remove(pool, first) == 0);
    assert(re_rows_remove(pool, first) == 3);
    assert(re_rows_release_empty(pool, &released) == 0 && released == 513);
    assert(re_rows_release_empty(pool, &released) == 0 && released == 0);
    ReRowHandle fresh;
    assert(re_rows_add(pool, (const uint8_t*) &input, sizeof(input), &fresh) == 0);
    assert(fresh.index == first.index && fresh.generation != first.generation);
    assert(re_rows_read(pool, first, (uint8_t*) &output, sizeof(output), &cut) == 3);
    assert(re_rows_write(pool, first, (const uint8_t*) &input, sizeof(input)) == 3);
    assert(re_rows_borrow(pool, first, &borrowed) == 3);
    /* C named binding consumes actual Rust-owned row bytes. Detach borrower first. */
    ReVariableRegistry *names = nullptr;
    assert(re_variables_new(2, &names) == 0);
    assert(re_rows_borrow(pool, fresh, &borrowed) == 0);
    size_t index = 99;
    assert(re_variables_add(names, (const uint8_t*) "health", 6, borrowed, &index) == 0);
    assert(re_variables_find(names, (const uint8_t*) "health", 6, &index) == 0);
    const ReVariableSlot *slot = nullptr;
    assert(re_variables_slot(names, index, &slot) == 0);
    assert((*slot).pointer == borrowed);
    memcpy(&output, (*slot).pointer, sizeof(output));
    assert(output.value == input.value);
    re_variables_drop(names); /* all borrowers detach before Rust storage drop */
    uint64_t old_owner = fresh.owner;
    re_rows_drop(pool);
    pool = nullptr;
    assert(ReRowPool(sizeof(input), 256, 2, &pool) == 0);
    assert(re_rows_add(pool, (const uint8_t*) &input, sizeof(input), &fresh) == 0);
    assert(fresh.owner != old_owner);
    assert(re_rows_remove(pool, saved) == 3); /* dead-owner identity, not a stale owner pointer */
    re_rows_drop(pool);
    re_rows_drop(other);
}

/* Executes all deterministic owner scenarios with assertions retained. */
int main(void) {
    layout();
    geometry_rejections();
    storage_contract();
    return 0;
}

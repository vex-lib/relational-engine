// Relational Engine production ForeignMemory owner test.
// (Per-File Battle Test Law). mem.c is the R2 substrate every block rides on,
// so the battle rows are deliberately harsh:
//   - VALUE BOUNDARY: 0-length, nullptr getters, exact length round trip;
//   - ALIGNMENT FLOOR: every payload (slab, bump and transient) is 32-aligned,
//     including an over-aligned type and a block that follows a 16-byte one;
//   - OVERFLOW GUARD: a size past UINT32_MAX is refused, not truncated;
//   - POINTER LEGITIMACY: a foreign aligned pointer and a freed header are
//     rejected without a blind dereference — proven in an isolated child so a
//     broken guard aborts the child, not this suite;
//   - FAILURE ATOMICITY: a refused realloc leaves the original intact;
//   - RESOURCE / LIFETIME: double-free safety, freeAll, arena isolation, the
//     arena destroy guards, and the transient generation contract.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "type/type.h"

#include "nio/mem.h"
#include "oop/type.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

#define GAP(cond, msg)                                                     \
    do {                                                                   \
        if (!(cond))                                                       \
            printf("KNOWN GAP (unproved): %s\n", msg);                     \
    } while (0)

static _Alignas(16) uint8_t g_foreign[64];

// A type that asks for more than the platform allocator promises (16).
typedef struct Over32 { _Alignas(32) uint8_t bytes[32]; } Over32;

int main(void) {
    CHECK(Memory_init(0));                                   // lazy default arena

    // --- Value boundary: nullptr getters are the zero mapping.
    CHECK(Memory_length(nullptr) == 0);

    CHECK(Memory_type(nullptr) == 0);

    Memory_free(nullptr);                                    // no-op

    Memory_realloc(nullptr, 0);                              // nullptr source, 0 Bytes

    // --- Happy path + exact length round trip + the 32-byte payload floor.
    void *p = Memory_alloc(ID_INT, 16);

    CHECK(p != nullptr);

    CHECK(((uintptr_t) p & 31) == 0);

    CHECK(Memory_length(p) == 16);

    CHECK(Memory_type(p) == ID_INT);

    memset(p, 0xA5, 16);

    // A block after the 16-byte one keeps the floor: the old 16-byte stride
    // would have left this payload 16 (mod 32).
    void *p_next = Memory_alloc(ID_LONG, 16);

    CHECK(p_next != nullptr);

    CHECK(((uintptr_t) p_next & 31) == 0);

    // An over-aligned type is safe through the allocator, on the bump path too
    // (payloads past the largest slab class never see a slab).
    CHECK(_Alignof(Over32) == 32);

    CHECK(sizeof(Over32) == 32);

    void *over_slab = Memory_alloc(ID_LONG, sizeof(Over32));

    CHECK(over_slab != nullptr);

    CHECK(((uintptr_t) over_slab & 31) == 0);

    for (int i = 0; i < 3; i++) {
        void *over_bump = Memory_alloc(ID_LONG, 5000u + (size_t) i * 16u);

        CHECK(over_bump != nullptr);

        CHECK(((uintptr_t) over_bump & 31) == 0);
    }

    void *zero = Memory_alloc(ID_BYTE, 0);                   // zero-length is legal

    CHECK(zero != nullptr);

    CHECK(Memory_length(zero) == 0);

    // --- Overflow guard: past UINT32_MAX must be refused, never truncated.
    CHECK(Memory_alloc(ID_INT, (size_t) UINT32_MAX + 1u) == nullptr);

    CHECK(Memory_alloc(ID_INT, (size_t) -1) == nullptr);

    // --- Pointer legitimacy: a foreign aligned pointer is refused, not read.
    CHECK(Memory_length(g_foreign) == 0);

    CHECK(Memory_type(g_foreign) == 0);

    Memory_free(g_foreign);                                  // no-op

    CHECK(Memory_similar(p, g_foreign) == false);

    CHECK(Memory_similar(g_foreign, p) == false);

    CHECK(Memory_similar(p, nullptr) == false);

    // --- Identity, never content: same type is similar across lengths.
    void *p2 = Memory_alloc(ID_INT, 64);

    CHECK(p2 != nullptr);

    CHECK(Memory_similar(p, p2));                            // same type id

    void *other = Memory_alloc(ID_LONG, 16);

    CHECK(Memory_similar(p, other) == false);                // different type id

    CHECK(Memory_similar(p, p) == true);                     // identity

    // --- Failure atomicity: a refused realloc leaves the original intact.
    void *havoc = Memory_realloc(p, (size_t) -1);

    CHECK(havoc == nullptr);

    CHECK(Memory_length(p) == 16);                           // unchanged

    for (size_t i = 0; i < 16; i++) 
        CHECK(((uint8_t*) p)[i] == 0xA5);                    // whole payload intact

    // --- realloc preserves type + contents across a grow.
    void *grown = Memory_realloc(p, 128);

    CHECK(grown != nullptr);

    CHECK(grown != p);

    CHECK(Memory_length(grown) == 128);

    CHECK(Memory_type(grown) == ID_INT);

    for (size_t i = 0; i < 16; i++) 
        CHECK(((uint8_t*) grown)[i] == 0xA5);

    p = grown;

    (void) p;

    // --- findAll counts live blocks of one type, honours the output cap.
    Memory_freeAll();

    for (int i = 0; i < 3; i++) 
        CHECK(Memory_alloc(0xABCDu, 8) != nullptr);

    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 3);         // count-only mode

    CHECK(Memory_findAll(ID_INT, nullptr, 0) == 0);          // none of that type

    void *found[8] = {0};

    CHECK(Memory_findAll(0xABCDu, found, 1) == 3);           // still counts all...

    CHECK(found[0] != nullptr);                              // ...but writes <= max

    Memory_free(found[0]);

    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 2);

    // --- Freed header is illegitimate (sugar cleared) — no read, no count.
    CHECK(Memory_length(found[0]) == 0);

    Memory_free(found[0]);                                   // double free is a no-op

    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 2);         // unchanged

    Memory_freeAll();

    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 0);         // all reclaimed

    // --- Arena isolation: allocate in B, free B, default untouched.
    MemoryArena *b = MemoryArena_create(64u * 1024u * 1024u);

    CHECK(b != nullptr);

    CHECK(MemoryArena_capacity(b) == 64u * 1024u * 1024u);

    void *bp = MemoryArena_alloc(b, 0xB0B0u, 32);

    CHECK(bp != nullptr);

    CHECK(Memory_type(bp) == 0xB0B0u);

    CHECK(MemoryArena_activeBytes(b) >= 32);

    CHECK(Memory_findAll(0xB0B0u, nullptr, 0) == 0);         // not in default

    CHECK(MemoryArena_findAll(b, 0xB0B0u, nullptr, 0) == 1);

    CHECK(MemoryArena_alloc(nullptr, 0, 8) == nullptr);      // null arena refused

    CHECK(MemoryArena_realloc(b, bp, 96) != nullptr);        // cross-arena routing

    MemoryArena_freeAll(b);

    CHECK(MemoryArena_activeBytes(b) == 0);

    MemoryArena_destroy(b);

    MemoryArena_destroy(Memory_defaultArena());              // guarded: no-op

    CHECK(Memory_alloc(ID_INT, 8) != nullptr);               // default still lives

    // --- Transient (frame/scratch) arena: lifetime + generation contract.
    CHECK(!Memory_initTransient(SIZE_MAX));

    CHECK(!Memory_initTransient(SIZE_MAX - 1u));

    CHECK(Memory_initTransient(4096));

    void *tp = Transient_alloc(ID_INT, 16);

    CHECK(tp != nullptr);

    CHECK(((uintptr_t) tp & 31) == 0);

    CHECK(Transient_contains(tp));

    CHECK(Memory_length(tp) == 16);

    CHECK(Memory_getLifetime(tp) == MEMORY_LIFETIME_TRANSIENT);

    void *perm = Memory_alloc(ID_INT, 8);

    CHECK(Memory_getLifetime(perm) == MEMORY_LIFETIME_PERMANENT);

    CHECK(Memory_getLifetime(nullptr) == MEMORY_LIFETIME_UNKNOWN);

    CHECK(Memory_getLifetime(g_foreign) == MEMORY_LIFETIME_UNKNOWN);

    Memory_free(tp);                                         // refused: transient-owned

    CHECK(Transient_contains(tp));                           // still live in the frame

    CHECK(Transient_alloc(ID_INT, 4096u + 64u) == nullptr);  // past capacity

    uint32_t gen = Transient_getGeneration();

    Transient_reset();

    CHECK(Transient_getGeneration() == gen + 1);

    CHECK(!Transient_contains(tp));                          // frame rewinded

    // --- Overflow Guard Law: rejected sizes preserve headers, contents, cursor
    // and generation, and a later valid scratch allocation still succeeds.
    uint8_t *before = Transient_alloc(ID_INT, 16);

    CHECK(before != nullptr);

    memset(before, 0xA5, 16);

    gen = Transient_getGeneration();

    CHECK(Transient_alloc(ID_INT, SIZE_MAX) == nullptr);

    CHECK(Transient_alloc(ID_INT, SIZE_MAX - 15u) == nullptr);

    CHECK(Transient_alloc(ID_INT, (size_t) UINT32_MAX + 1u) == nullptr);

    CHECK(Transient_alloc(ID_INT, UINT32_MAX) == nullptr); // valid width, no capacity

    CHECK(Memory_length(before) == 16 && before[0] == 0xA5);

    CHECK(Transient_getGeneration() == gen);

    uint8_t *after = Transient_alloc(ID_INT, 16);

    CHECK(after == before + MEMORY_HEADER_SIZE + 32);

    Transient_reset();

    // --- Pointer legitimacy in an isolated child: a wild aligned address must
    //     be rejected by range, never dereferenced (a bad guard aborts the child).
    pid_t pid = fork();

    if (pid == 0) {
        Memory_free((void*) (uintptr_t) 0x1000);

        volatile size_t len = Memory_length((void*) (uintptr_t) 0x1000);

        _exit(len == 0 ? 0 : 1);
    }

    CHECK(pid > 0);

    int status = 0;

    CHECK(waitpid(pid, &status, 0) == pid);

    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    if (g_failures == 0) {
        printf("mem_test: all assertions held\n");

        return 0;
    }

    printf("mem_test: %d FAILURES\n", g_failures);

    return 1;
}

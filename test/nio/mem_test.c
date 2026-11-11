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
//   - RESOURCE / LIFETIME: d64-free safety, freeAll, arena isolation, the
//     arena destroy guards, and the transient generation contract.

#include "vex.h"
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "nio/mem.h"

enum {
    TEST_TYPE_A = 1u,
    TEST_TYPE_B = 2u,
    TEST_TYPE_C = 3u,
};

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

static _Alignas(16) u8 g_foreign[64];

// A type that asks for more than the platform allocator promises (16).
typedef struct Over32 { _Alignas(32) u8 bytes[32]; } Over32;

int main(void) {
    CHECK(Memory_init(0));

    CHECK(Memory_length(nullptr) == 0);

    CHECK(Memory_type(nullptr) == 0);

    Memory_free(nullptr);

    Memory_realloc(nullptr, 0);

    void *p = Memory_alloc(TEST_TYPE_A, 16);

    CHECK(p != nullptr);

    CHECK(((uintptr_t) p & 31) == 0);

    CHECK(Memory_length(p) == 16);

    CHECK(Memory_type(p) == TEST_TYPE_A);

    memset(p, 0xA5, 16);

    void *p_next = Memory_alloc(TEST_TYPE_B, 16);

    CHECK(p_next != nullptr);

    CHECK(((uintptr_t) p_next & 31) == 0);

    CHECK(_Alignof(Over32) == 32);

    CHECK(sizeof(Over32) == 32);

    void *over_slab = Memory_alloc(TEST_TYPE_B, sizeof(Over32));

    CHECK(over_slab != nullptr);

    CHECK(((uintptr_t) over_slab & 31) == 0);

    for (int i = 0; i < 3; i++) {
        void *over_bump = Memory_alloc(TEST_TYPE_B, 5000u + (bytes) i * 16u);

        CHECK(over_bump != nullptr);

        CHECK(((uintptr_t) over_bump & 31) == 0);
    }

    void *zero = Memory_alloc(TEST_TYPE_C, 0);

    CHECK(zero != nullptr);

    CHECK(Memory_length(zero) == 0);

    CHECK(Memory_alloc(TEST_TYPE_A, (bytes) UINT32_MAX + 1u) == nullptr);

    CHECK(Memory_alloc(TEST_TYPE_A, (bytes) -1) == nullptr);

    CHECK(Memory_length(g_foreign) == 0);

    CHECK(Memory_type(g_foreign) == 0);

    Memory_free(g_foreign);

    CHECK(Memory_similar(p, g_foreign) == false);

    CHECK(Memory_similar(g_foreign, p) == false);

    CHECK(Memory_similar(p, nullptr) == false);

    void *p2 = Memory_alloc(TEST_TYPE_A, 64);

    CHECK(p2 != nullptr);

    CHECK(Memory_similar(p, p2));

    void *other = Memory_alloc(TEST_TYPE_B, 16);

    CHECK(Memory_similar(p, other) == false);

    CHECK(Memory_similar(p, p) == true);

    void *havoc = Memory_realloc(p, (bytes) -1);

    CHECK(havoc == nullptr);

    CHECK(Memory_length(p) == 16);

    for (bytes i = 0; i < 16; i++)
        CHECK(((u8*) p)[i] == 0xA5);

    void *grown = Memory_realloc(p, 128);

    CHECK(grown != nullptr);

    CHECK(grown != p);

    CHECK(Memory_length(grown) == 128);

    CHECK(Memory_type(grown) == TEST_TYPE_A);

    for (bytes i = 0; i < 16; i++)
        CHECK(((u8*) grown)[i] == 0xA5);

    p = grown;

    (void) p;

    Memory_freeAll();

    for (int i = 0; i < 3; i++)
        CHECK(Memory_alloc(0xABCDu, 8) != nullptr);

    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 3);

    CHECK(Memory_findAll(TEST_TYPE_A, nullptr, 0) == 0);

    void *found[8] = {0};

    CHECK(Memory_findAll(0xABCDu, found, 1) == 3);

    CHECK(found[0] != nullptr);

    Memory_free(found[0]);

    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 2);

    CHECK(Memory_length(found[0]) == 0);

    Memory_free(found[0]);

    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 2);

    Memory_freeAll();

    CHECK(Memory_findAll(0xABCDu, nullptr, 0) == 0);

    MemoryArena *b = MemoryArena_create(64u * 1024u * 1024u);

    CHECK(b != nullptr);

    CHECK(MemoryArena_capacity(b) == 64u * 1024u * 1024u);

    void *bp = MemoryArena_alloc(b, 0xB0B0u, 32);

    CHECK(bp != nullptr);

    CHECK(Memory_type(bp) == 0xB0B0u);

    CHECK(MemoryArena_activeBytes(b) >= 32);

    CHECK(Memory_findAll(0xB0B0u, nullptr, 0) == 0);

    CHECK(MemoryArena_findAll(b, 0xB0B0u, nullptr, 0) == 1);

    CHECK(MemoryArena_alloc(nullptr, 0, 8) == nullptr);

    CHECK(MemoryArena_realloc(b, bp, 96) != nullptr);

    MemoryArena_freeAll(b);

    CHECK(MemoryArena_activeBytes(b) == 0);

    MemoryArena_destroy(b);

    MemoryArena_destroy(Memory_defaultArena());

    CHECK(Memory_alloc(TEST_TYPE_A, 8) != nullptr);

    CHECK(!Memory_initTransient(SIZE_MAX));

    CHECK(!Memory_initTransient(SIZE_MAX - 1u));

    CHECK(Memory_initTransient(4096));

    void *tp = Transient_alloc(TEST_TYPE_A, 16);

    CHECK(tp != nullptr);

    CHECK(((uintptr_t) tp & 31) == 0);

    CHECK(Transient_contains(tp));

    CHECK(Memory_length(tp) == 16);

    CHECK(Memory_getLifetime(tp) == MEMORY_LIFETIME_TRANSIENT);

    void *perm = Memory_alloc(TEST_TYPE_A, 8);

    CHECK(Memory_getLifetime(perm) == MEMORY_LIFETIME_PERMANENT);

    CHECK(Memory_getLifetime(nullptr) == MEMORY_LIFETIME_UNKNOWN);

    CHECK(Memory_getLifetime(g_foreign) == MEMORY_LIFETIME_UNKNOWN);

    Memory_free(tp);

    CHECK(Transient_contains(tp));

    CHECK(Transient_alloc(TEST_TYPE_A, 4096u + 64u) == nullptr);

    u32 gen = Transient_getGeneration();

    Transient_reset();

    CHECK(Transient_getGeneration() == gen + 1);

    CHECK(!Transient_contains(tp));

    u8 *before = Transient_alloc(TEST_TYPE_A, 16);

    CHECK(before != nullptr);

    memset(before, 0xA5, 16);

    gen = Transient_getGeneration();

    CHECK(Transient_alloc(TEST_TYPE_A, SIZE_MAX) == nullptr);

    CHECK(Transient_alloc(TEST_TYPE_A, SIZE_MAX - 15u) == nullptr);

    CHECK(Transient_alloc(TEST_TYPE_A, (bytes) UINT32_MAX + 1u) == nullptr);

    CHECK(Transient_alloc(TEST_TYPE_A, UINT32_MAX) == nullptr);

    CHECK(Memory_length(before) == 16 && before[0] == 0xA5);

    CHECK(Transient_getGeneration() == gen);

    u8 *after = Transient_alloc(TEST_TYPE_A, 16);

    CHECK(after == before + MEMORY_HEADER_SIZE + 32);

    Transient_reset();

    pid_t pid = fork();

    if (pid == 0) {
        Memory_free((void*) (uintptr_t) 0x1000);

        volatile bytes len = Memory_length((void*) (uintptr_t) 0x1000);

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

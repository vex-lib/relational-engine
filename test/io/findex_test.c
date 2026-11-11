// re/test/io/findex_test.c — FIndex owner test.
#include <assert.h>

#include "io/file/file.h"
#include "nio/mem.h"

int main(void) {
    FIndex index = FIndex_create(4);
    assert(FIndex_live(&index));
    assert(FIndex_count(&index) == 0);

    u64 offset = 0;
    u64 length = 0;
    assert(!FIndex_find(&index, 1, &offset, &length));
    assert(FIndex_insert(&index, 1, 100, 10));
    assert(FIndex_insert(&index, 2, 200, 20));
    assert(FIndex_count(&index) == 2);
    assert(FIndex_find(&index, 1, &offset, &length) && offset == 100 && length == 10);
    assert(FIndex_find(&index, 2, &offset, &length) && offset == 200 && length == 20);

    assert(FIndex_insert(&index, 1, 111, 11));
    assert(FIndex_find(&index, 1, &offset, &length) && offset == 111 && length == 11);
    assert(FIndex_count(&index) == 2);

    FIndex_remove(&index, 1);
    assert(!FIndex_find(&index, 1, &offset, &length));
    assert(FIndex_count(&index) == 1);

    for (u64 i = 0; i < 200; i++)
        assert(FIndex_insert(&index, i + 1000, i, i + 1));
    for (u64 i = 0; i < 200; i++)
        assert(FIndex_find(&index, i + 1000, &offset, &length) && offset == i && length == i + 1);

    FIndex empty = FIndex_create(0);
    assert(!FIndex_live(&empty));
    assert(FIndex_insert(&empty, 7, 70, 7));
    assert(FIndex_live(&empty));
    assert(FIndex_find(&empty, 7, &offset, &length) && offset == 70 && length == 7);
    FIndex_shutdown(&empty);

    MemoryArena *arena = MemoryArena_create(64u * 1024u * 1024u);
    assert(arena);
    FIndex arenad = FIndex_createArena(2, arena);
    assert(FIndex_live(&arenad));
    assert(FIndex_insert(&arenad, 42, 420, 42));
    assert(FIndex_find(&arenad, 42, &offset, &length) && offset == 420 && length == 42);
    FIndex_shutdown(&arenad);
    MemoryArena_destroy(arena);

    FIndex_shutdown(&index);
    assert(!FIndex_live(&index));
    assert(!FIndex_insert(nullptr, 0, 0, 0));
    assert(!FIndex_find(nullptr, 0, nullptr, nullptr));
    FIndex_remove(nullptr, 0);
    FIndex_shutdown(nullptr);
    assert(FIndex_count(nullptr) == 0);
    assert(!FIndex_live(nullptr));

    Memory_freeAll();
    return 0;
}

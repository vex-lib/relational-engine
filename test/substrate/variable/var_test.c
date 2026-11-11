#include <assert.h>
#include <string.h>

#include "substrate/variable/var.h"
#include "vex.h"

static int g_calls;
static uintptr_t g_last;

static void observer(var *self, uintptr_t value, void *context) {
    (void) self;
    g_calls += 1;
    g_last = value;
    assert(context == &g_calls);
}

int main(void) {
    MemoryArena *arena = MemoryArena_create(64u * 1024u * 1024u);
    assert(arena);

    var *v = var("health", arena);
    assert(v);
    assert(strcmp(var_name(v), "health") == 0);
    assert(var_value(v) == 0);
    assert(var_observerCount(v) == 0);
    assert(var_arena(v) == arena);

    assert(var_subscribe(v, observer, &g_calls));
    assert(var_set(v, 42));
    assert(g_calls == 1 && g_last == 42);

    char buffer[128];
    bool truncated = true;
    var_toString(v, buffer, sizeof buffer, &truncated);
    assert(!truncated && strstr(buffer, "health") != nullptr);

    var_unsubscribe(v, observer, &g_calls);
    assert(var_observerCount(v) == 0);
    assert(var_set(v, 7));
    assert(g_calls == 1);
    assert(var_value(v) == 7);

    assert(!var_subscribe(nullptr, observer, nullptr));
    assert(!var_set(nullptr, 1));
    var_unsubscribeAll(v);
    var_unsubscribeAll(nullptr);

    MemoryArena_destroy(arena);
    Memory_freeAll();
    return 0;
}

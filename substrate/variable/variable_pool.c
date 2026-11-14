#include "substrate/variable/variable_pool.h"

#include <string.h>

#include "lang/annotation.h"
#include "atomic/atomic.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: StringPoolService
 * ============================================================================
 * Process-wide interned string pool: each distinct name is stated once in a
 * fixed 32-byte slot ([ptr][str1][str2][str3]) and shared by pointer, never
 * copied — name identity is inherently global, so two arenas interning the
 * same name must share rather than duplicate. The static table is arena-
 * backed: zero-initialized statics fail closed pre-init (magic gate), and
 * teardown composes with the owning arena. Lookup runs binary search over a
 * separate sorted u32 index so growth never moves a slot's identity; growth
 * copies slots verbatim then rewrites every self link. Every public entry
 * serializes on one spinlock (cold paths only; lock order is pool-then-arena
 * with no reverse path, so no cycle).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: StringPoolService (substrate/variable/variable_pool.c — static intern table)
 * ============================================================================
 * Process-wide interned string pool: names stated once in fixed 32-byte
 * slots ([ptr][str1][str2][str3]), shared by pointer, never copied. Static
 * table, arena backing: zero-initialized statics fail closed pre-init, and
 * teardown composes with the owning arena (pool memory dies with it).
 * Ordering lives in a separate sorted u32 index so binary search never
 * moves a slot; growth copies slots verbatim then rewrites every self link.
 * Every public entry serializes on one spinlock (cold paths only; the
 * critical section is bounded by pool size, lock order is pool-then-arena
 * with no reverse path, so no cycle).
 *
 * STRUCT FIELDS (Mirroring substrate/variable/variable_pool.h):
 * ----------------------------------------------------------------------------
 *   StringSlot {
 *     u64 self;  // intrusive validity: must equal own address
 *     char name[24];  // NUL-terminated, zero-padded, exact Bytes
 *   }
 *
 * PRIVATE HELPERS (kept file-local pure-data only, each with full fields):
 * ----------------------------------------------------------------------------
 *   StringPoolState {
 *     u32 magic;     // STRING_POOL_MAGIC when live (fail-closed gate)
 *     MemoryArena *arena; // backing arena (borrowed; teardown frees all)
 *     StringSlot *slots;  // slot array (append-only; may move on grow)
 *     u32 *order;    // sorted slot indices
 *     u32 count;     // live slots
 *     u32 capacity;  // allocated slots
 *   }
 *
 * SLOT RECORD (owned by this service, behaviorless, see variable_pool.h):
 * ----------------------------------------------------------------------------
 *   StringSlot self;    // u64 + intrusive validity, equals own address
 *   StringSlot name;    // char[24] + NUL-terminated Bytes, zero-padded
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - StringPool_init(arena)
 *   - StringPool_shutdown()
 *
 * Core Functions:
 *   - StringPool_intern(str)     : interning with dedup, -1 on reject
 *   - StringPool_find(str)       : slot index or -1
 *
 * Getters:
 *   - StringPool_slot(index)
 *   - StringPool_name(index)
 *   - StringPool_count()
 *   - StringPool_isSlot(ptr)
 * ============================================================================
 */

#define STRING_POOL_MAGIC 0x504F4F4Cu

typedef struct StringPoolState {
    u32 magic;     // STRING_POOL_MAGIC when live (fail-closed gate)
    MemoryArena *arena; // backing arena (borrowed; teardown frees all)
    StringSlot *slots;  // slot array (append-only; may move on grow)
    u32 *order;    // sorted slot indices
    u32 count;     // live slots
    u32 capacity;  // allocated slots
} StringPoolState;

static StringPoolState s_pool;
static SpinLock s_lock = SPIN_LOCK_INIT;

// Binary search over the sorted index (Java Arrays.binarySearch courtesy:
// found -> slot index; absent -> -(insertion point) - 1). Compares full
// zero-padded 24-byte names through one indirection per probe. Lock held.
static i32 search_order(const char key[24]) {
    u32 *order = s_pool.order;
    StringSlot *slots = s_pool.slots;
    i32 lo = 0;
    i32 hi = (i32) s_pool.count;
    while (lo < hi) {
        i32 mid = lo + (hi - lo) / 2;
        u32 s = order[mid];
        int c = memcmp(slots[s].name, key, 24);
        if (c < 0)
            lo = mid + 1;
        else if (c > 0)
            hi = mid;
        else
            return (i32) s;
    }
    return -lo - 1;
}

// f64s the slot + index arrays (cold path only, lock held). Copies slots
// verbatim then rewrites every self link — absolute addresses change on move.
static bool grow() {
    u32 cap = s_pool.capacity * 2;
    if (cap < s_pool.capacity)
        return false;
    if (cap == 0)
        cap = STRING_POOL_DEFAULT_CAPACITY;
    StringSlot *slots = (StringSlot*) MemoryArena_alloc(s_pool.arena, TYPE_STRING_POOL, (bytes) cap * sizeof(StringSlot));
    if (!slots)
        return false;
    u32 *order = (u32*) MemoryArena_alloc(s_pool.arena, TYPE_VARIABLE_POOL_ORDER, (bytes) cap * sizeof(u32));
    if (!order) {
        MemoryArena_free(s_pool.arena, slots);
        return false;
    }
    for (u32 i = 0; i < s_pool.count; i++) {
        slots[i] = s_pool.slots[i];
        slots[i].self = (u64) (pointer) &slots[i];
        order[i] = s_pool.order[i];
    }
    if (s_pool.slots)
        MemoryArena_free(s_pool.arena, s_pool.slots);
    if (s_pool.order)
        MemoryArena_free(s_pool.arena, s_pool.order);
    s_pool.slots = slots;
    s_pool.order = order;
    s_pool.capacity = cap;
    return true;
}

bool StringPool_init(MemoryArena *arena) {
    if (!arena)
        return false;
    SpinLock_lock(&s_lock);
    if (s_pool.magic == STRING_POOL_MAGIC) {
        SpinLock_unlock(&s_lock);
        return true;
    }
    s_pool.magic = 0;
    s_pool.arena = arena;
    s_pool.slots = nullptr;
    s_pool.order = nullptr;
    s_pool.count = 0;
    s_pool.capacity = 0;
    bool ok = grow();
    if (ok)
        s_pool.magic = STRING_POOL_MAGIC;
    else
        s_pool.arena = nullptr;
    SpinLock_unlock(&s_lock);
    return ok;
}

fn StringPool_shutdown() {
    SpinLock_lock(&s_lock);
    s_pool.magic = 0;
    if (s_pool.arena) {
        if (s_pool.slots)
            MemoryArena_free(s_pool.arena, s_pool.slots);
        if (s_pool.order)
            MemoryArena_free(s_pool.arena, s_pool.order);
    }
    s_pool.slots = nullptr;
    s_pool.order = nullptr;
    s_pool.count = 0;
    s_pool.capacity = 0;
    s_pool.arena = nullptr;
    SpinLock_unlock(&s_lock);
}

;;INTENTION("23-char names: fixed 32B slots (24B name + 8B self) keep the pool indexable with O(1) validity checks; longer names rejected cold — class/widget/probe names are short by convention and silent truncation would corrupt identity")
i32 StringPool_intern(const char *str) {
    if (!str || str[0] == '\0')
        return -1;
    bytes len = strlen(str);
    if (len > STRING_POOL_NAME_MAX)
        return -1;
    char key[24];
    memset(key, 0, sizeof(key));
    memcpy(key, str, len);
    SpinLock_lock(&s_lock);
    if (s_pool.magic != STRING_POOL_MAGIC) {
        SpinLock_unlock(&s_lock);
        return -1;
    }
    i32 at = search_order(key);
    if (at >= 0) {
        SpinLock_unlock(&s_lock);
        return at;
    }
    if (s_pool.count >= s_pool.capacity && !grow()) {
        SpinLock_unlock(&s_lock);
        return -1;
    }
    at = search_order(key);
    if (at >= 0) {
        SpinLock_unlock(&s_lock);
        return at;
    }
    u32 pos = (u32) (-(at + 1));
    u32 idx = s_pool.count;
    StringSlot *slots = s_pool.slots;
    u32 *order = s_pool.order;
    memset(&slots[idx], 0, sizeof(slots[idx]));
    memcpy(slots[idx].name, key, 24);
    slots[idx].self = (u64) (pointer) &slots[idx];
    memmove(&order[pos + 1], &order[pos], ((bytes) s_pool.count - pos) * sizeof(u32));
    order[pos] = idx;
    s_pool.count++;
    SpinLock_unlock(&s_lock);
    return (i32) idx;
}

i32 StringPool_find(const char *str) {
    if (!str || str[0] == '\0')
        return -1;
    bytes len = strlen(str);
    if (len > STRING_POOL_NAME_MAX)
        return -1;
    char key[24];
    memset(key, 0, sizeof(key));
    memcpy(key, str, len);
    SpinLock_lock(&s_lock);
    if (s_pool.magic != STRING_POOL_MAGIC) {
        SpinLock_unlock(&s_lock);
        return -1;
    }
    i32 at = search_order(key);
    SpinLock_unlock(&s_lock);
    return at >= 0 ? at : -1;
}

const StringSlot *StringPool_slot(u32 index) {
    SpinLock_lock(&s_lock);
    const StringSlot *out = nullptr;
    if (s_pool.magic == STRING_POOL_MAGIC && index < s_pool.count)
        out = &s_pool.slots[index];
    SpinLock_unlock(&s_lock);
    return out;
}

const char *StringPool_name(u32 index) {
    const StringSlot *slot = StringPool_slot(index);
    return slot ? (*slot).name : nullptr;
}

u32 StringPool_count() {
    SpinLock_lock(&s_lock);
    u32 n = s_pool.magic == STRING_POOL_MAGIC ? s_pool.count : 0;
    SpinLock_unlock(&s_lock);
    return n;
}

bool StringPool_isSlot(const_any ptr) {
    if (!ptr)
        return false;
    SpinLock_lock(&s_lock);
    bool ok = false;
    if (s_pool.magic == STRING_POOL_MAGIC) {
        const StringSlot *slot = (const StringSlot*) ptr;
        if ((*slot).self == (u64) (pointer) ptr) {
            const StringSlot *base = s_pool.slots;
            ok = slot >= base && slot < base + s_pool.capacity;
        }
    }
    SpinLock_unlock(&s_lock);
    return ok;
}

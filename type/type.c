#include "type/type.h"
#include "lang/annotation.h"

#include "vex.h"
#include <string.h>

#include "nio/mem.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Type
 * ============================================================================
 * The R2 type-id ALGEBRA, owned by Relational Engine. The 64-bit id encoding
 * (project byte, form nibble, per-project class number, BE6C sugar) and its
 * pure readers live in type/type.h; this file holds the parent-chain resolver.
 *
 * Each project keeps its OWN class registry in its own *-type.h and grants its
 * parent chain once here, keyed by the project byte. Vexspoke's own legacy
 * bare-id chain is granted through Type_registerBareParents — so this file
 * never hardcodes a project's class numbers and carries no project registry.
 * The slate is a growable arena-backed table (the Dynamic Scalability &
 * Anti-Hardcoding Law); an unregistered project resolves every class as a root.
 *
 * Registration is a COLD seam: it validates out-of-range entries and rejects
 * cyclic or self-referential chains atomically (Failure Atomicity and Recovery
 * Law). The Type_isA walk is BOUNDED by the project's chain length, so no table
 * can spin it (the Bounded Wait Law).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Type (type/type)
 * ============================================================================
 * The project-agnostic id algebra + per-project parent-chain resolver.
 *
 * PRIVATE HELPERS (kept file-local pure-data only, the Single Class Per File
 * Law private-helper doctrine):
 * ----------------------------------------------------------------------------
 *   TypeParentsRow {              // one registered project's parent chain
 *     u64 proj;              // owning project byte (0 = empty slot)
 *     const u32 *parents;    // parents[i] = parent class # of class # i
 *     u32 count;             // 0 = root; rows past count = root
 *   }
 *   g_typeTables                // growable slate, first-match, doubling on demand
 *   g_bareParents/g_bareCount   // vexspoke bare-id / PROJ_VEXSPOKE legacy chain
 *   growSlate(needed)           // exponential growth, arena-backed
 *   findTable(proj)             // row lookup for the project byte
 *   chainIsValid(parents,count) // bounds + cycle/self-reference rejection
 *   chainCount(proj)            // registered chain length (walk budget)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 *   Core:
 *     - Type_registerParents(proj, parents, count)     // seam: per-project chain
 *     - Type_registerBareParents(parents, count)       // seam: bare/vexspoke chain
 *     - Type_getParentClass(classId)
 *     - Type_arch(classId)
 *     - Type_isA(classId, ancestorId)                  // bounded walk
 * ============================================================================
 */

// One registered project's parent chain. Private slot record: pure data, no
// Class_* behavior of its own (all behavior hangs off Type).
typedef struct TypeParentsRow {
    u64 proj;               // owning project byte; 0 = empty slot
    const u32 *parents;     // parents[i] = parent class # of class # i
    u32 count;              // 0 row = root; rows past count = root
} TypeParentsRow;

// Growable registration slate (the Dynamic Scalability & Anti-Hardcoding Law):
// starts empty, f64s exponentially on demand, arena-backed. Rows beyond
// g_typeTableCount are never scanned, so stale Bytes are harmless. Allocated
// untyped (id 0): the slate is engine-internal and carries no project identity.
static TypeParentsRow *g_typeTables = nullptr;
static bytes g_typeTableCount = 0;
static bytes g_typeTableCap = 0;

// Vexspoke's own bare-id / PROJ_VEXSPOKE legacy chain, granted once at startup.
static const u32 *g_bareParents = nullptr;
static u32 g_bareCount = 0;

// Grow the slate to at least `needed` rows (doubling, cold start 8). On OOM
// the slate is left untouched and registration fails; the caller drops.
/** Grow the parent-chain registration table to at least needed rows. */
static bool growSlate(bytes needed) {
    if (needed <= g_typeTableCap) return true;
    bytes newCap = (g_typeTableCap == 0) ? 8 : g_typeTableCap * 2;
    while (newCap < needed) newCap *= 2;
    TypeParentsRow *nb = (TypeParentsRow*) Memory_alloc(
        0u, newCap * sizeof(TypeParentsRow));
    if (nb == nullptr) return false;
    if (g_typeTables != nullptr && g_typeTableCap > 0)
        memcpy(nb, g_typeTables, g_typeTableCap * sizeof(TypeParentsRow));
    g_typeTables = nb;
    g_typeTableCap = newCap;
    return true;
}

/** Find a registered parent-chain row by its project identifier. */
static const TypeParentsRow *findTable(u64 proj) {
    for (bytes i = 0; i < g_typeTableCount; ++i) {
        const TypeParentsRow *row = &g_typeTables[i];
        if ((*row).proj == proj)
            return row;
    }
    return nullptr;
}

// A chain is valid when every listed parent is a class number inside the table
// and no class loops back on itself. A self-parent (parents[i] == i) and a
// mutual cycle both fail here, so Type_isA can never be handed a spinning
// table (the Bounded Wait Law).
/** Validate parent indices and reject multi-class cycles before chain publication. */
static bool chainIsValid(const u32 *parents, u32 count) {
    if (parents == nullptr)
        return count == 0u;
    if (count == 0u)
        return true;
    for (u32 i = 1u; i < count; ++i) {
        if (parents[i] >= count)
            return false;
    }
    for (u32 i = 1u; i < count; ++i) {
        u32 cur = i;
        u32 hops = 0u;
        while (parents[cur] != 0u && parents[cur] != cur) {
            if (++hops > count)
                return false;
            cur = parents[cur];
        }
    }
    return true;
}

/** Return the registered parent-chain length used to bound Type_isA traversal. */
static u32 chainCount(u64 proj) {
    if (proj == 0u || proj == PROJ_VEXSPOKE)
        return g_bareCount;
    const TypeParentsRow *row = findTable(proj);
    return row != nullptr ? (*row).count : 0u;
}

/** Register or replace one non-Vexspoke project's validated class-parent table. */
bool Type_registerParents(u64 proj, const u32 *parents, u32 count) {
    if (proj == 0u)
        return false;
    if ((proj & MASK_PROJECT) != proj)
        return false;
    if (proj == PROJ_VEXSPOKE)
        return false;
    if (!chainIsValid(parents, count))
        return false;
    for (bytes i = 0; i < g_typeTableCount; ++i) {
        TypeParentsRow *row = &g_typeTables[i];
        if ((*row).proj == proj) {
            (*row).parents = parents;
            (*row).count = count;
            return true;
        }
    }
    if (!growSlate(g_typeTableCount + 1u)) return false;
    TypeParentsRow *row = &g_typeTables[g_typeTableCount++];
    (*row).proj = proj;
    (*row).parents = parents;
    (*row).count = count;
    return true;
}

/** Register or replace the validated parent table for bare/Vexspoke class ids. */
bool Type_registerBareParents(const u32 *parents, u32 count) {
    if (!chainIsValid(parents, count))
        return false;
    g_bareParents = parents;
    g_bareCount = count;
    return true;
}

/** Return a class's parent id, treating absent or unregistered entries as roots. */
u64 Type_getParentClass(u64 classId) {
    u64 proj = classId & MASK_PROJECT;
    u64 cls = classId & MASK_CLASS;
    if (proj == 0u || proj == PROJ_VEXSPOKE) {
        if (g_bareParents != nullptr && cls != 0u && cls < g_bareCount) {
            u32 parent = g_bareParents[cls];
            return parent != 0u ? (u64) parent : cls;
        }
        return cls;
    }
    const TypeParentsRow *row = findTable(proj);
    if (row != nullptr && cls != 0u && cls < (*row).count) {
        u32 parent = (*row).parents[cls];
        return parent != 0u ? (u64) parent : cls;
    }
    return cls;
}

/** Map an encoded project byte to its architecture id; bare or unknown projects default to Vexspoke. */
u64 Type_arch(u64 classId) {
    const u64 proj = classId & MASK_PROJECT;
    if (proj == PROJ_VEXSPOKE)
        return ARCH_VEXSPOKE;
    if (proj == PROJ_GRAPHVEX)
        return ARCH_GRAPHVEX;
    if (proj == PROJ_HOTCWAP)
        return ARCH_HOTCWAP;
    if (proj == PROJ_DARLING)
        return ARCH_DARLING;
    if (proj == PROJ_API_HAVEN)
        return ARCH_APIHAVEN;
    if (proj == PROJ_DARKBASE)
        return ARCH_DARKBASE;
    if (proj == PROJ_SAMPLERATE)
        return ARCH_SAMPLERATE;
    return ARCH_VEXSPOKE;
}

/** Test class ancestry within the same project using a walk bounded by its registered chain length. */
u32 Type_isA(u64 classId, u64 ancestorId) {
    u64 proj = classId & MASK_PROJECT;
    u64 target = ancestorId & MASK_CLASS;
    u64 current = classId;
    u32 budget = chainCount(proj) + 2u;
    while (budget-- > 0u) {
        u64 cc = current & MASK_CLASS;
        if (cc == target)
            return 1;
        u64 parent = Type_getParentClass(current);
        if ((parent & MASK_CLASS) == cc)
            return 0;
        current = (parent & MASK_CLASS) | proj;
    }
    return 0;
}

// Cold type predicates. The bit readers stay header-side as macros.

u32 Type_isStruct(u64 form) {
    return form == FORM_STRUCT_SINGLETON || form == FORM_STRUCT_ARRAY
        || form == FORM_STRUCT_POINTER;
}

u32 Type_isSingleton(u64 typeId) {
    return (typeId & MASK_FORM) == FORM_SINGLETON;
}

u32 Type_isArray(u64 typeId) {
    u64 form = typeId & MASK_FORM;
    return form == FORM_ARRAY || form == FORM_ARRAY_SOA || form == FORM_ARRAY_AOS || form == FORM_STRUCT_COEXISTENT;
}

u32 Type_isPointer(u64 typeId) {
    return (typeId & MASK_FORM) == FORM_POINTER;
}

u32 Type_isStructSingleton(u64 typeId) {
    return (typeId & MASK_FORM) == FORM_STRUCT_SINGLETON;
}

u32 Type_isStructArray(u64 typeId) {
    u64 form = typeId & MASK_FORM;
    return form == FORM_STRUCT_ARRAY || form == FORM_ARRAY_SOA || form == FORM_ARRAY_AOS || form == FORM_STRUCT_COEXISTENT;
}

u32 Type_isStructSOA(u64 typeId) {
    return (typeId & MASK_FORM) == FORM_ARRAY_SOA;
}

u32 Type_isStructAOS(u64 typeId) {
    return (typeId & MASK_FORM) == FORM_ARRAY_AOS;
}

u32 Type_isStructCoexistent(u64 typeId) {
    return (typeId & MASK_FORM) == FORM_STRUCT_COEXISTENT;
}

u32 Type_isStructPointer(u64 typeId) {
    return (typeId & MASK_FORM) == FORM_STRUCT_POINTER;
}

u32 Type_isPrimitive(u64 typeId) {
    u64 form = typeId & MASK_FORM;
    return form == FORM_SINGLETON || form == FORM_ARRAY || form == FORM_POINTER;
}
inline u32 Type_isStructSingleton(u64 typeId);

inline u32 Type_isStructArray(u64 typeId);

inline u32 Type_isStructSOA(u64 typeId);

inline u32 Type_isStructAOS(u64 typeId);

inline u32 Type_isStructCoexistent(u64 typeId);

inline u32 Type_isStructPointer(u64 typeId);

inline u32 Type_isPrimitive(u64 typeId);

inline u32 Type_isGlobal(u64 typeId);

inline u32 Type_isLocale(u64 typeId);

inline u32 Type_isTransient(u64 typeId);

inline u32 Type_isProactive(u64 typeId);

inline u32 Type_isReactive(u64 typeId);

inline u32 Type_isProbable(u64 typeId);

u32 Type_isGlobal(u64 typeId) {
    return (typeId & MASK_MODIFIER) == MOD_GLOBAL;
}

u32 Type_isLocale(u64 typeId) {
    return (typeId & MASK_MODIFIER) == MOD_LOCALE;
}

u32 Type_isTransient(u64 typeId) {
    return (typeId & MASK_MODIFIER) == MOD_TRANSIENT;
}

u32 Type_isProactive(u64 typeId) {
    return (typeId & MASK_WRAPPER_1) == WRAP_PROACTIVE;
}

u32 Type_isReactive(u64 typeId) {
    return (typeId & MASK_WRAPPER_1) == WRAP_REACTIVE;
}

u32 Type_isProbable(u64 typeId) {
    return (typeId & MASK_WRAPPER_2) == WRAP2_PROBABLE;
}

u32 Type_isProbableObjects(u64 typeId) {
    return (typeId & MASK_WRAPPER_2) == WRAP2_PROBABLE_OBJECTS;
}

u32 Type_isFuture(u64 typeId) {
    return (typeId & MASK_WRAPPER_2) == WRAP2_FUTURE;
}

u32 Type_isChoice(u64 typeId) {
    return (typeId & MASK_WRAPPER_2) == WRAP2_CHOICE;
}

u32 Type_isVexspoke(u64 classId) {
    return Type_arch(classId) == ARCH_VEXSPOKE;
}

u32 Type_isHotcwap(u64 classId) {
    return Type_arch(classId) == ARCH_HOTCWAP;
}

u32 Type_isDarling(u64 classId) {
    return Type_arch(classId) == ARCH_DARLING;
}

u32 Type_isDarkbase(u64 classId) {
    return Type_arch(classId) == ARCH_DARKBASE;
}

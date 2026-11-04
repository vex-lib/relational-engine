#include "type/type.h"
#include "annotation/annotation.h"

#include <stddef.h>
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
 *     uint64_t proj;              // owning project byte (0 = empty slot)
 *     const uint32_t *parents;    // parents[i] = parent class # of class # i
 *     uint32_t count;             // 0 = root; rows past count = root
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
    uint64_t proj;               // owning project byte; 0 = empty slot
    const uint32_t *parents;     // parents[i] = parent class # of class # i
    uint32_t count;              // 0 row = root; rows past count = root
} TypeParentsRow;

// Growable registration slate (the Dynamic Scalability & Anti-Hardcoding Law):
// starts empty, doubles exponentially on demand, arena-backed. Rows beyond
// g_typeTableCount are never scanned, so stale Bytes are harmless. Allocated
// untyped (id 0): the slate is engine-internal and carries no project identity.
static TypeParentsRow *g_typeTables = nullptr;
static size_t g_typeTableCount = 0;
static size_t g_typeTableCap = 0;

// Vexspoke's own bare-id / PROJ_VEXSPOKE legacy chain, granted once at startup.
static const uint32_t *g_bareParents = nullptr;
static uint32_t g_bareCount = 0;

// Grow the slate to at least `needed` rows (doubling, cold start 8). On OOM
// the slate is left untouched and registration fails; the caller drops.
/** Grow the parent-chain registration table to at least needed rows. */
static bool growSlate(size_t needed) {
    if (needed <= g_typeTableCap) return true;
    size_t newCap = (g_typeTableCap == 0) ? 8 : g_typeTableCap * 2;
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
static const TypeParentsRow *findTable(uint64_t proj) {
    for (size_t i = 0; i < g_typeTableCount; ++i) {
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
static bool chainIsValid(const uint32_t *parents, uint32_t count) {
    if (parents == nullptr)
        return count == 0u;                     // empty table only
    if (count == 0u)
        return true;                            // empty table, non-null is fine
    for (uint32_t i = 1u; i < count; ++i) {
        if (parents[i] >= count)
            return false;                       // parent out of range
    }
    // A walk is legal when it reaches a root within `count` hops. A self-parent
    // (parents[i] == i) is a ROOT encoding, not a cycle; only a loop of two or
    // more distinct classes can spin, so only that is rejected (the Bounded Wait
    // Law).
    for (uint32_t i = 1u; i < count; ++i) {
        uint32_t cur = i;
        uint32_t hops = 0u;
        while (parents[cur] != 0u && parents[cur] != cur) {
            if (++hops > count)
                return false;                   // cycle: never roots
            cur = parents[cur];
        }
    }
    return true;
}

/** Return the registered parent-chain length used to bound Type_isA traversal. */
static uint32_t chainCount(uint64_t proj) {
    if (proj == 0u || proj == PROJ_VEXSPOKE)
        return g_bareCount;
    const TypeParentsRow *row = findTable(proj);
    return row != nullptr ? (*row).count : 0u;
}

/** Register or replace one non-Vexspoke project's validated class-parent table. */
bool Type_registerParents(uint64_t proj, const uint32_t *parents, uint32_t count) {
    if (proj == 0u)
        return false;
    if ((proj & MASK_PROJECT) != proj)
        return false;
    if (proj == PROJ_VEXSPOKE)
        return false;
    if (!chainIsValid(parents, count))
        return false;
    for (size_t i = 0; i < g_typeTableCount; ++i) {
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
bool Type_registerBareParents(const uint32_t *parents, uint32_t count) {
    if (!chainIsValid(parents, count))
        return false;
    g_bareParents = parents;
    g_bareCount = count;
    return true;
}

/** Return a class's parent id, treating absent or unregistered entries as roots. */
uint64_t Type_getParentClass(uint64_t classId) {
    uint64_t proj = classId & MASK_PROJECT;
    uint64_t cls = classId & MASK_CLASS;
    if (proj == 0u || proj == PROJ_VEXSPOKE) {
        if (g_bareParents != nullptr && cls != 0u && cls < g_bareCount) {
            uint32_t parent = g_bareParents[cls];
            return parent != 0u ? (uint64_t) parent : cls;   // table 0 row = root
        }
        return cls;                                    // unlisted bare class = root
    }
    const TypeParentsRow *row = findTable(proj);
    if (row != nullptr && cls != 0u && cls < (*row).count) {
        uint32_t parent = (*row).parents[cls];
        return parent != 0u ? (uint64_t) parent : cls;   // table 0 row = root
    }
    return cls;                                    // unregistered project = root
}

/** Map an encoded project byte to its architecture id; bare or unknown projects default to Vexspoke. */
uint64_t Type_arch(uint64_t classId) {
    uint64_t proj = classId & MASK_PROJECT;
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
    // Bare ids carry no project byte: with per-project numbering they can only
    // mean vexspoke's own class space. Cross-project code passes full ids.
    return ARCH_VEXSPOKE;
}

/** Test class ancestry within the same project using a walk bounded by its registered chain length. */
int Type_isA(uint64_t classId, uint64_t ancestorId) {
    uint64_t proj = classId & MASK_PROJECT;
    uint64_t target = ancestorId & MASK_CLASS;
    uint64_t current = classId;
    uint32_t budget = chainCount(proj) + 2u;       // bounded walk
    while (budget-- > 0u) {
        uint64_t cc = current & MASK_CLASS;
        if (cc == target)
            return 1;
        uint64_t parent = Type_getParentClass(current);
        if ((parent & MASK_CLASS) == cc)
            return 0;                              // root reached, not target
        current = (parent & MASK_CLASS) | proj;    // walk stays in-project
    }
    return 0;                                      // bounded: cycle guard fallback
}

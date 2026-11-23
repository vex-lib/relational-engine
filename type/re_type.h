#ifndef TYPE_TYPE_H
#define TYPE_TYPE_H

#include "vex.h"

/**
 * re_type.h, the R2 type-id ALGEBRA (Relational Engine owner).
 *
 * The project-agnostic half of the One Type Registry Law: the 64-bit id
 * encoding, its masks, and the pure readers. This is the Relational Engine's
 * own *-type.h: it carries the shared algebra plus this project's class
 * registry. It must never include another project's registry (for example
 * Vexspoke's oop/type.h), or the registry that includes it would recurse.
 * The 16-byte block-header contract lives with the allocator in nio/mem.h
 * (MemoryHeader); the old vexspoke TypeHeader was dead and is removed.
 *
 * 0xF'PRPR'M'W1'W2'BE6C'CCCCCC
 * Bits: 4 + 8 + 4 + 4 + 4 + 16 + 24 = 64.
 *
 * Field map:
 * 0xF PRPR M W1 W2 BE6C CCCCCC
 * | |    | |  |  |    `-------- class      (24 bits, which struct, per-project)
 * | |    | |  |  `------------- sugar      ("векс" / vex)
 * | |    | |  `---------------- wrapper 2  (probable/future/choice)
 * | |    | `------------------- wrapper 1  (proactive/reactive)
 * | |    `--------------------- modifier   (global/locale/transient)
 * | `-------------------------- project    (8 bits, owning repo, 256 projects)
 * `--------------------------- form       (singleton/array/..., struct layouts)
 *
 * BE6C is the author's stylized Cyrillic "векс" / vex signature, stamped as
 * recognizable sugar in every id. It is a fixed format marker, not a checksum
 * or a stale-generation check. A [typeId:8][pointer-or-inline-value:8] slot
 * therefore stays 16 Bytes.
 *
 * Uniform per-project numbering: every project numbers its classes starting at
 * 1; the project byte disambiguates identical local numbers across repos. Bare
 * ID_* constants carry no project byte; they are interpreted against the
 * registry that defines them. This header defines the Relational Engine's own
 * registry below. Cross-project dispatch ships full type ids
 * (PROJ_* | FORM_* | class), and Type_arch reads the byte back.
 *
 * The parent-chain resolver (Type_registerParents / Type_getParentClass /
 * Type_isA) is project-agnostic: a project registers its chain once (keyed by
 * its project byte) and this file walks it. Vexspoke's own legacy bare-id
 * chain is registered through Type_registerBareParents, so this algebra never
 * hardcodes a project's class numbers.
 */

#define MASK_FORM       0xF'00'0'0'0'0000'000000ULL
#define MASK_PROJECT    0x0'FF'0'0'0'0000'000000ULL
#define MASK_MODIFIER   0x0'00'F'0'0'0000'000000ULL
#define MASK_WRAPPER_1  0x0'00'0'F'0'0000'000000ULL
#define MASK_WRAPPER_2  0x0'00'0'0'F'0000'000000ULL
#define MASK_SUGAR      0x0'00'0'0'0'FFFF'000000ULL
;;INTENTION("BE6C is the author's stylized Cyrillic vex signature sugar, a fixed format marker, not a checksum")
#define SUGAR_VEX       0x0'00'0'0'0'BE6C'000000ULL /** "векс" */
#define MASK_CLASS      0x0'00'0'0'0'0000'FFFFFFULL

#define FORM_SINGLETON          0x1'00'0'0'0'0000'000000ULL
#define FORM_ARRAY              0x2'00'0'0'0'0000'000000ULL
#define FORM_POINTER            0x3'00'0'0'0'0000'000000ULL
#define FORM_STRUCT_SINGLETON   0x4'00'0'0'0'0000'000000ULL
#define FORM_STRUCT_ARRAY       0x5'00'0'0'0'0000'000000ULL
#define FORM_STRUCT_POINTER     0x6'00'0'0'0'0000'000000ULL
#define FORM_ARRAY_SOA          0x7'00'0'0'0'0000'000000ULL
#define FORM_ARRAY_AOS          0x8'00'0'0'0'0000'000000ULL
#define FORM_STRUCT_COEXISTENT  0x9'00'0'0'0'0000'000000ULL

/** Project byte: which repo owns the class. 256 projects fit the same stack. */
#define PROJ_GENERIC             0x0'00'0'0'0'0000'000000ULL /** zero lol */
#define PROJ_VEXSPOKE            0x0'01'0'0'0'0000'000000ULL
#define PROJ_GRAPHVEX            0x0'02'0'0'0'0000'000000ULL
#define PROJ_HOTCWAP             0x0'03'0'0'0'0000'000000ULL
#define PROJ_DARLING             0x0'04'0'0'0'0000'000000ULL
#define PROJ_API_HAVEN           0x0'05'0'0'0'0000'000000ULL
#define PROJ_DARKBASE            0x0'06'0'0'0'0000'000000ULL
#define PROJ_RELATIONAL_ENGINE   0x0'07'0'0'0'0000'000000ULL
#define PROJ_SAMPLERATE          0x0'08'0'0'0'0000'000000ULL

#define MOD_GLOBAL     0x0'00'1'0'0'0000'000000ULL
#define MOD_LOCALE     0x0'00'2'0'0'0000'000000ULL
#define MOD_TRANSIENT  0x0'00'3'0'0'0000'000000ULL

#define WRAP_PROACTIVE  0x0'00'0'1'0'0000'000000ULL
#define WRAP_REACTIVE   0x0'00'0'2'0'0000'000000ULL

#define WRAP2_PROBABLE          0x0'00'0'0'1'0000'000000ULL
#define WRAP2_PROBABLE_OBJECTS  0x0'00'0'0'2'0000'000000ULL
#define WRAP2_FUTURE            0x0'00'0'0'3'0000'000000ULL
#define WRAP2_CHOICE            0x0'00'0'0'4'0000'000000ULL

/**
 * Architecture layer owning an id (ARCH_*, one per project byte). Bare ids
 * report ARCH_VEXSPOKE, since bare numbers always mean vexspoke's class space.
 */
#define ARCH_VEXSPOKE  1u
#define ARCH_HOTCWAP   2u
#define ARCH_DARLING   3u
#define ARCH_GRAPHVEX  4u
#define ARCH_APIHAVEN  5u
#define ARCH_DARKBASE  6u
#define ARCH_SAMPLERATE 7u

#define Type_make(proj, form, classId) (((proj) & MASK_PROJECT) | ((form) & MASK_FORM) | SUGAR_VEX | ((classId) & MASK_CLASS))

/** Shared scalar & plain-container class IDs (compatible with vexspoke's legacy table). */
#define ID_INT          0x0001u
#define ID_LONG         0x0002u
#define ID_f64          0x0003u
#define ID_FIXED64      0x0004u
#define ID_FLOAT        0x0005u
#define ID_SHORT        0x0006u
#define ID_SPINLOCK     0x0007u
#define ID_RING_BUFFER  0x0008u
#define ID_BIT8         0x0009u
#define ID_BIT16        0x000Au
#define ID_BIT32        0x000Bu
#define ID_BIT64        0x000Cu
#define ID_VEC2         0x000Du
#define ID_VEC3         0x000Eu
#define ID_ENTITY       0x000Fu
#define ID_BYTE         0x0010u
#define ID_STRING       0x0011u
#define ID_VARIABLE     0x0016u
#define ID_ARRAYS       0x0018u
#define ID_STRIDE       0x001Bu
#define ID_LIST         0x001Cu
#define ID_MAP          0x001Du
#define ID_SET          0x001Eu
#define ID_STACK        0x001Fu
#define ID_DEQUE        0x0020u
#define ID_MIN_HEAP     0x0021u
#define ID_SPARSE_SET   0x0022u
#define ID_RANDOM       0x0023u
#define ID_INDEX_RANDOM 0x0024u
#define ID_QUEUE        0x0027u
#define ID_ARGUMENTS    0x0028u
#define ID_VEC4         0x0029u
#define ID_MAT3         0x002Au
#define ID_MAT4         0x002Bu
#define ID_QUATERNION   0x002Cu
#define ID_POINT        0x002Du
#define ID_RECT         0x002Eu
#define ID_CHUNKED_LIST 0x008Au

#define TYPE_INT_SINGLETON     (SUGAR_VEX | PROJ_VEXSPOKE | FORM_SINGLETON | ID_INT)
#define TYPE_INT_ARRAY         (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_INT)
#define TYPE_INT_POINTER       (SUGAR_VEX | PROJ_VEXSPOKE | FORM_POINTER   | ID_INT)
#define TYPE_LONG_ARRAY        (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_LONG)
#define TYPE_FLOAT_ARRAY       (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_FLOAT)
#define TYPE_f64_ARRAY         (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_f64)
#define TYPE_BYTE_ARRAY        (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_BYTE)
#define TYPE_STRING_ARRAY      (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_STRING)
#define TYPE_SPIN_LOCK         (SUGAR_VEX | PROJ_VEXSPOKE | FORM_SINGLETON | ID_SPINLOCK)
#define TYPE_RING_BUFFER       (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_RING_BUFFER)
#define TYPE_LIST              (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_LIST)
#define TYPE_ARRAY             (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_ARRAYS)
#define TYPE_CHUNKED_LIST      (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_CHUNKED_LIST)
#define TYPE_STACK             (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_STACK)
#define TYPE_DEQUE             (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_DEQUE)
#define TYPE_QUEUE             (SUGAR_VEX | PROJ_VEXSPOKE | FORM_ARRAY     | ID_QUEUE)
#define TYPE_MAP               (SUGAR_VEX | PROJ_VEXSPOKE | FORM_POINTER   | ID_MAP)
#define TYPE_SET               (SUGAR_VEX | PROJ_VEXSPOKE | FORM_POINTER   | ID_SET)
#define TYPE_VEC2_SINGLETON    (SUGAR_VEX | PROJ_VEXSPOKE | FORM_SINGLETON | ID_VEC2)
#define TYPE_VEC3_SINGLETON    (SUGAR_VEX | PROJ_VEXSPOKE | FORM_SINGLETON | ID_VEC3)
#define TYPE_VEC4_SINGLETON    (SUGAR_VEX | PROJ_VEXSPOKE | FORM_SINGLETON | ID_VEC4)
#define TYPE_MAT3_SINGLETON    (SUGAR_VEX | PROJ_VEXSPOKE | FORM_SINGLETON | ID_MAT3)
#define TYPE_MAT4_SINGLETON    (SUGAR_VEX | PROJ_VEXSPOKE | FORM_SINGLETON | ID_MAT4)
#define TYPE_QUATERNION_SINGLETON (SUGAR_VEX | PROJ_VEXSPOKE | FORM_SINGLETON | ID_QUATERNION)
#define TYPE_POINT_SINGLETON   (SUGAR_VEX | PROJ_VEXSPOKE | FORM_SINGLETON | ID_POINT)
#define TYPE_RECT_SINGLETON    (SUGAR_VEX | PROJ_VEXSPOKE | FORM_SINGLETON | ID_RECT)

/**
 * TYPES FOR RELATIONAL ENGINE SPECIFICALLY!!!
 *
 * Relational Engine class registry (project byte 0x07). The class numbers are
 * local to this project; the project byte disambiguates them from another
 * repository's registry. These replace the legacy Vexspoke ids that re used to
 * borrow for its own classes.
 */

#define ID_VARIABLE_SLOT       13u
#define ID_STRING_POOL         14u
#define ID_VARIABLE_HASH_MAP   15u
#define ID_VARIABLE_MINI_MAP   16u
#define ID_SHELF               17u
#define ID_SHELF_NODE          18u
#define ID_VARIABLE_POOL_ORDER 19u
#define ID_RELATIONAL_STRING    20u
#define ID_SYMBOL_TABLE         21u
#define ID_VARCHAR              22u

#define TYPE_VARIABLE_SLOT              Type_make(PROJ_RELATIONAL_ENGINE, FORM_STRUCT_SINGLETON, ID_VARIABLE_SLOT)
#define TYPE_STRING_POOL                Type_make(PROJ_RELATIONAL_ENGINE, FORM_ARRAY,            ID_STRING_POOL)
#define TYPE_VARIABLE_HASH_MAP          Type_make(PROJ_RELATIONAL_ENGINE, FORM_STRUCT_SINGLETON, ID_VARIABLE_HASH_MAP)
#define TYPE_VARIABLE_MINI_MAP          Type_make(PROJ_RELATIONAL_ENGINE, FORM_STRUCT_SINGLETON, ID_VARIABLE_MINI_MAP)
#define TYPE_SHELF                      Type_make(PROJ_RELATIONAL_ENGINE, FORM_STRUCT_SINGLETON, ID_SHELF)
#define TYPE_SHELF_NODE                 Type_make(PROJ_RELATIONAL_ENGINE, FORM_STRUCT_SINGLETON, ID_SHELF_NODE)
#define TYPE_VARIABLE_POOL_ORDER        Type_make(PROJ_RELATIONAL_ENGINE, FORM_ARRAY,            ID_VARIABLE_POOL_ORDER)
#define TYPE_RELATIONAL_STRING          Type_make(PROJ_RELATIONAL_ENGINE, FORM_ARRAY,            ID_RELATIONAL_STRING)
#define TYPE_SYMBOL_TABLE               Type_make(PROJ_RELATIONAL_ENGINE, FORM_STRUCT_SINGLETON, ID_SYMBOL_TABLE)
#define TYPE_VARCHAR                    Type_make(PROJ_RELATIONAL_ENGINE, FORM_STRUCT_SINGLETON, ID_VARCHAR)

#define Type_class(typeId) ((typeId) & MASK_CLASS)

#define Type_form(typeId) ((typeId) & MASK_FORM)

#define Type_project(typeId) ((typeId) & MASK_PROJECT)

u32 Type_isStruct(u64 form);
u32 Type_isSingleton(u64 typeId);
u32 Type_isArray(u64 typeId);
u32 Type_isPointer(u64 typeId);
u32 Type_isStructSingleton(u64 typeId);
u32 Type_isStructArray(u64 typeId);
u32 Type_isStructSOA(u64 typeId);
u32 Type_isStructAOS(u64 typeId);
u32 Type_isStructCoexistent(u64 typeId);
u32 Type_isStructPointer(u64 typeId);
u32 Type_isPrimitive(u64 typeId);
u32 Type_isGlobal(u64 typeId);
u32 Type_isLocale(u64 typeId);
u32 Type_isTransient(u64 typeId);
u32 Type_isProactive(u64 typeId);
u32 Type_isReactive(u64 typeId);
u32 Type_isProbable(u64 typeId);
u32 Type_isProbableObjects(u64 typeId);
u32 Type_isFuture(u64 typeId);
u32 Type_isChoice(u64 typeId);
u64 Type_arch(u64 classId);
u32 Type_isVexspoke(u64 classId);
u32 Type_isHotcwap(u64 classId);
u32 Type_isDarling(u64 classId);
u32 Type_isDarkbase(u64 classId);
/**
 * One registered project's parent chain.
 *
 * u64 proj           - owning project byte (0 = empty slot)
 * const u32 *parents - parents[i] = parent class number of class i
 * u32 count          - 0 row = root; rows past count = root
 */
typedef struct TypeParentsRow {
    u64 proj;
    const u32 *parents;
    u32 count;
} TypeParentsRow;

bool Type_registerParents(u64 proj, const u32 *parents, u32 count);
bool Type_registerBareParents(const u32 *parents, u32 count);
u64 Type_getParentClass(u64 classId);
u32 Type_isA(u64 classId, u64 ancestorId);
#endif
#ifndef RE_TEST_TYPES_H
#define RE_TEST_TYPES_H

#include "vex.h"
#include "io/file/file.h"
#include "struct/list.h"
#include <pthread.h>

/**
 * Test record collector for log parser tests.
 *
 * i32 count    - number of captured records
 * i32 kinds[8] - captured record kinds
 * i64 v0[8]    - captured first values
 * i64 ts[8]    - captured timestamps
 */
typedef struct Collector {
    i32 count;
    i32 kinds[8];
    i64 v0[8];
    i64 ts[8];
} Collector;

/**
 * Test label struct for memory/string ownership tests.
 *
 * utf8 *text    - label text buffer
 * bool ownsText - true when this label owns text
 */
typedef struct TestLabel {
    utf8 *text;
    bool ownsText;
} TestLabel;

/**
 * 32-byte aligned test payload for alignment tests.
 *
 * u8 bytes[32] - 32-byte aligned payload buffer
 */
typedef struct Over32 {
    _Alignas(32) u8 bytes[32];
} Over32;

/**
 * Test watcher callback recorder for file event tests.
 *
 * i32 count           - number of events observed
 * utf8 lastRel[128]   - last relative path observed
 * FileEventType lastType - last event type observed
 * i64 lastEpoch       - last event epoch timestamp
 * i64 lastSize        - last reported file byte count
 */
typedef struct WatchRec {
    i32 count;
    utf8 lastRel[128];
    FileEventType lastType;
    i64 lastEpoch;
    i64 lastSize;
} WatchRec;

/**
 * Named test suite entry for the re_test runner.
 *
 * string name - suite identifier string
 * fn (*run)()      - suite entry point function pointer
 */
typedef struct SuiteEntry {
    string name;
    fn (*run)();
} SuiteEntry;

/**
 * Visitor probe state for variable hash map iteration tests.
 *
 * u32 visited    - count of visited entries
 * u32 sum        - accumulated sum of visited values
 * bool sawDotted - true if a dotted key was encountered
 */
typedef struct VisitProbe {
    u32 visited;
    u32 sum;
    bool sawDotted;
} VisitProbe;

/**
 * 16-byte test row for chunked list tests.
 *
 * u32 a - first lane
 * u32 b - second lane
 * u32 c - third lane
 * u32 d - fourth lane
 */
typedef struct Row16 {
    u32 a;
    u32 b;
    u32 c;
    u32 d;
} Row16;

/**
 * Chunk chain traversal probe for paged chunked list tests.
 *
 * u32 chunks - number of chunks visited
 * bool ok    - true if chain invariants held
 */
typedef struct ChainProbe {
    u32 chunks;
    bool ok;
} ChainProbe;

/**
 * Worker thread argument for concurrent list insertion tests.
 *
 * List *list            - target list under test
 * pthread_mutex_t *lock - external mutex guarding list writes
 * u32 base              - starting element value for this worker
 * u32 count             - number of elements to insert
 */
typedef struct AddArg {
    List *list;
    pthread_mutex_t *lock;
    u32 base;
    u32 count;
} AddArg;

/**
 * Wide element struct for ring buffer copy tests.
 *
 * u64 a       - first 64-bit payload field
 * u64 b       - second 64-bit payload field
 * utf8 tag[7] - short NUL-terminated tag
 */
typedef struct Big {
    u64 a;
    u64 b;
    utf8 tag[7];
} Big;

/**
 * Custom vertex struct for stride registration tests.
 *
 * f32 x      - X coordinate (16-byte aligned)
 * f32 y      - Y coordinate
 * f32 z      - Z coordinate
 * u32 color  - packed color value
 * d64 weight - vertex weight
 */
typedef struct TestCustomVertex {
    alignas(16) f32 x;
    f32 y;
    f32 z;
    u32 color;
    d64 weight;
} TestCustomVertex;

/**
 * 64-byte over-aligned test payload verifying >32B alignment rejection.
 *
 * u8 bytes[64] - 64-byte aligned payload buffer
 */
typedef struct Over64 {
    _Alignas(64) u8 bytes[64];
} Over64;

/**
 * Multi-field test struct carrying a constructor macro to prove macro-named
 * classes pass cleanly into ArrayList(ListDemoRecord) / LinkedList / PagedList.
 *
 * u32 id     - logical record id
 * f32 score  - floating-point weight
 * u64 tag    - 64-bit payload tag
 */
typedef struct ListDemoRecord {
    u32 id;
    f32 score;
    u64 tag;
} ListDemoRecord;

ListDemoRecord ListDemoRecord_3(u32 id, f32 score, u64 tag);
#define ListDemoRecord(...) CONSTRUCTOR_DISPATCH(ListDemoRecord, __VA_ARGS__)

/**
 * Accumulator context for List_forEach verification.
 *
 * bytes visits - number of elements visited
 * u64 sum      - sum of visited u32 elements
 */
typedef struct ListVisitCtx {
    bytes visits;
    u64 sum;
} ListVisitCtx;

/**
 * Shared context for the 8-thread atomic double-free race probe.
 *
 * MemoryArena *arena - target arena owning sharedPtr
 * any sharedPtr      - shared pointer raced across worker threads
 * au32 *go           - atomic start barrier
 * au32 *trueCount    - atomic count of successful frees (must equal 1)
 * au32 *falseCount   - atomic count of rejected double-frees (must equal T-1)
 */
typedef struct ExRaceCtx {
    MemoryArena *arena;
    any sharedPtr;
    au32 *go;
    au32 *trueCount;
    au32 *falseCount;
} ExRaceCtx;

/**
 * Visitor state for Map_forEach verification.
 *
 * bytes visits - number of visited entries
 * u64 keySum   - sum of visited u32 keys
 * u64 valSum   - sum of visited u64 values
 * u32 order[8] - first eight keys in visited order
 */
typedef struct MapVisitCtx {
    bytes visits;
    u64 keySum;
    u64 valSum;
    u32 order[8];
} MapVisitCtx;

/**
 * Visitor state for Set_forEach verification.
 *
 * bytes visits - number of visited elements
 * u64 sum      - sum of visited u32 elements
 * u32 order[8] - first eight elements in visited order
 */
typedef struct SetVisitCtx {
    bytes visits;
    u64 sum;
    u32 order[8];
} SetVisitCtx;

/**
 * Vertex payload struct for ArrayGraph and LinkedGraph tests.
 *
 * u32 code    - city code
 * string name - city name
 */
typedef struct GraphCity {
    u32 code;
    string name;
} GraphCity;

/**
 * Edge payload struct for ArrayGraph and LinkedGraph tests.
 *
 * f32 distance - road length in kilometers
 * u32 lanes    - road lane count
 */
typedef struct GraphRoad {
    f32 distance;
    u32 lanes;
} GraphRoad;

/**
 * Visitor state for Graph_forEachEdge verification.
 *
 * bytes visits      - number of visited edges
 * f32 totalDistance - accumulated road distance
 * u32 targets[8]    - visited target vertex ids
 */
typedef struct GraphVisitCtx {
    bytes visits;
    f32 totalDistance;
    u32 targets[8];
} GraphVisitCtx;

#endif

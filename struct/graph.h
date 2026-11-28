#ifndef STRUCT_GRAPH_H
#define STRUCT_GRAPH_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/collection.h"
#include "struct/layout.h"
#include "struct/list.h"

/**
 * struct/graph.h, the polymorphic Graph interface.
 *
 * Concrete graph backings (ArrayGraph, LinkedGraph) embed Graph at offset zero
 * and register a GraphOps vtable. Construct with vertex type V and optional
 * edge payload type E (defaults to f32 weight):
 *
 * Graph *g = ArrayGraph(City);
 * Graph *w = ArrayGraph(City, Road);
 * Graph *l = LinkedGraph(Node, f32);
 * u32 a = Graph_addVertex(g, paris);
 * u32 b = Graph_addVertex(g, lyon);
 * Graph_addEdge(g, a, b, 465.0f);
 * Graph_free(g);
 */

#define GRAPH_INVALID_VERTEX UINT32_MAX

/**
 * Concrete Graph backing discriminator stored in GraphOps.
 *
 * GRAPH_KIND_UNKNOWN - uninitialized or invalid backing
 * GRAPH_KIND_ARRAY   - contiguous vertex buffer + flat adjacency arrays (ArrayGraph)
 * GRAPH_KIND_LINKED  - pointer-stable linked vertex and edge nodes (LinkedGraph)
 */
;;CLASSNAME("GraphKind")
typedef enum GraphKind {
    GRAPH_KIND_UNKNOWN = 0,
    GRAPH_KIND_ARRAY = 1,
    GRAPH_KIND_LINKED = 2,
} GraphKind;

typedef struct Graph Graph;

typedef fn (*GraphEdgeVisitFn)(u32 from, u32 to, any edgePayload, bytes index, any userdata);

typedef u32 (*GraphAddVertexBytesFn)(Graph *self, const_any vertex, bytes vertexBytes);
typedef any (*GraphEmplaceVertexFn)(Graph *self, u32 *outId);
typedef any (*GraphVertexFn)(const Graph *self, u32 id);
typedef bool (*GraphWriteVertexBytesFn)(Graph *self, u32 id, const_any vertex, bytes vertexBytes);
typedef bool (*GraphAddEdgeBytesFn)(Graph *self, u32 from, u32 to, const_any edge, bytes edgeBytes);
typedef any (*GraphEdgeFn)(const Graph *self, u32 from, u32 to);
typedef bool (*GraphRemoveEdgeFn)(Graph *self, u32 from, u32 to);
typedef bytes (*GraphOutDegreeFn)(const Graph *self, u32 from);
typedef fn (*GraphForEachEdgeFn)(Graph *self, u32 from, GraphEdgeVisitFn visit, any userdata);
typedef List *(*GraphNeighborsFn)(const Graph *self, u32 from);
typedef fn (*GraphClearFn)(Graph *self);
typedef bytes (*GraphVertexCountFn)(const Graph *self);
typedef bytes (*GraphEdgeCountFn)(const Graph *self);
typedef fn (*GraphDestroyFn)(Graph *self);

/**
 * Polymorphic dispatch table implemented by every concrete Graph backing.
 *
 * GraphKind kind                           - concrete backing discriminator
 * string kindName                          - human-readable backing name
 * GraphAddVertexBytesFn addVertexBytes     - appends vertex and returns u32 id
 * GraphEmplaceVertexFn emplaceVertex       - appends zeroed vertex slot
 * GraphVertexFn vertex                     - returns pointer to vertex payload
 * GraphWriteVertexBytesFn writeVertexBytes - overwrites vertex payload by id
 * GraphAddEdgeBytesFn addEdgeBytes         - inserts or updates directed edge
 * GraphEdgeFn edge                         - looks up directed edge payload
 * GraphRemoveEdgeFn removeEdge             - removes directed edge
 * GraphOutDegreeFn outDegree               - outgoing edge count for vertex
 * GraphForEachEdgeFn forEachEdge           - visits outgoing edges of vertex
 * GraphNeighborsFn neighbors               - exports neighbor u32 ids as List
 * GraphClearFn clear                       - drops all vertices and edges
 * GraphVertexCountFn vertexCount           - total vertex count
 * GraphEdgeCountFn edgeCount               - total directed edge count
 * GraphDestroyFn destroy                   - frees all storage and handle
 */
;;CLASSNAME("GraphOps")
typedef struct GraphOps {
    GraphKind kind;
    string kindName;
    GraphAddVertexBytesFn addVertexBytes;
    GraphEmplaceVertexFn emplaceVertex;
    GraphVertexFn vertex;
    GraphWriteVertexBytesFn writeVertexBytes;
    GraphAddEdgeBytesFn addEdgeBytes;
    GraphEdgeFn edge;
    GraphRemoveEdgeFn removeEdge;
    GraphOutDegreeFn outDegree;
    GraphForEachEdgeFn forEachEdge;
    GraphNeighborsFn neighbors;
    GraphClearFn clear;
    GraphVertexCountFn vertexCount;
    GraphEdgeCountFn edgeCount;
    GraphDestroyFn destroy;
} GraphOps;

/**
 * Polymorphic Graph interface embedded at offset zero of every Graph backing.
 *
 * Collection collection      - embedded Collection header (offset 0)
 * const GraphOps *ops        - borrowed vtable describing the concrete backing
 * MemoryArena *arena         - borrowed MemoryArena owning allocations
 * ElementLayout vertexLayout - compile-time-captured vertex payload layout
 * ElementLayout edgeLayout   - compile-time-captured edge payload layout
 */
;;CLASSNAME("Graph")
typedef struct Graph {
    Collection collection;
    const GraphOps *ops;
    MemoryArena *arena;
    ElementLayout vertexLayout;
    ElementLayout edgeLayout;
} Graph;

bool Graph_initBase(Graph *self, u64 typeId, const GraphOps *ops,
                    ElementLayout vertexLayout, ElementLayout edgeLayout, MemoryArena *arena);

u32 Graph_addVertexBytes(Graph *self, const_any vertex, bytes vertexBytes);
any Graph_emplaceVertex(Graph *self, u32 *outId);
any Graph_vertex(const Graph *self, u32 id);
bool Graph_vertexCopyOut(const Graph *self, u32 id, any dest, bytes vertexBytes, string typeName);
bool Graph_setVertexBytes(Graph *self, u32 id, const_any vertex, bytes vertexBytes);
bool Graph_addEdgeBytes(Graph *self, u32 from, u32 to, const_any edge, bytes edgeBytes);
bool Graph_addBiEdgeBytes(Graph *self, u32 a, u32 b, const_any edge, bytes edgeBytes);
any Graph_edge(const Graph *self, u32 from, u32 to);
bool Graph_edgeCopyOut(const Graph *self, u32 from, u32 to, any dest, bytes edgeBytes, string typeName);
bool Graph_hasEdge(const Graph *self, u32 from, u32 to);
bool Graph_removeEdge(Graph *self, u32 from, u32 to);
bytes Graph_outDegree(const Graph *self, u32 from);
fn Graph_forEachEdge(Graph *self, u32 from, GraphEdgeVisitFn visit, any userdata);
List *Graph_neighbors(const Graph *self, u32 from);
fn Graph_clear(Graph *self);
fn Graph_free(Graph *self);

bytes Graph_vertexCount(const Graph *self);
bytes Graph_edgeCount(const Graph *self);
bool Graph_isEmpty(const Graph *self);
bytes Graph_vertexStride(const Graph *self);
bytes Graph_edgeStride(const Graph *self);
string Graph_vertexTypeName(const Graph *self);
string Graph_edgeTypeName(const Graph *self);
GraphKind Graph_kind(const Graph *self);
string Graph_kindName(const Graph *self);
MemoryArena *Graph_arena(const Graph *self);
const GraphOps *Graph_ops(const Graph *self);
ElementLayout Graph_vertexLayout(const Graph *self);
ElementLayout Graph_edgeLayout(const Graph *self);

;;TEST
u64 Graph_exhaustionCount(const Graph *self);

fn Graph_toString(const Graph *self, utf8 *dest, bytes cap, bool *outTruncated);
fn Graph_toStringStruct(const Graph *self, utf8 *dest, bytes cap, bool *outTruncated);

fn GraphOps_setKind(GraphOps *ops, GraphKind kind);
fn GraphOps_setKindName(GraphOps *ops, string kindName);
fn GraphOps_setAddVertexBytes(GraphOps *ops, GraphAddVertexBytesFn fnPtr);
fn GraphOps_setEmplaceVertex(GraphOps *ops, GraphEmplaceVertexFn fnPtr);
fn GraphOps_setVertex(GraphOps *ops, GraphVertexFn fnPtr);
fn GraphOps_setWriteVertexBytes(GraphOps *ops, GraphWriteVertexBytesFn fnPtr);
fn GraphOps_setAddEdgeBytes(GraphOps *ops, GraphAddEdgeBytesFn fnPtr);
fn GraphOps_setEdge(GraphOps *ops, GraphEdgeFn fnPtr);
fn GraphOps_setRemoveEdge(GraphOps *ops, GraphRemoveEdgeFn fnPtr);
fn GraphOps_setOutDegree(GraphOps *ops, GraphOutDegreeFn fnPtr);
fn GraphOps_setForEachEdge(GraphOps *ops, GraphForEachEdgeFn fnPtr);
fn GraphOps_setNeighbors(GraphOps *ops, GraphNeighborsFn fnPtr);
fn GraphOps_setClear(GraphOps *ops, GraphClearFn fnPtr);
fn GraphOps_setVertexCount(GraphOps *ops, GraphVertexCountFn fnPtr);
fn GraphOps_setEdgeCount(GraphOps *ops, GraphEdgeCountFn fnPtr);
fn GraphOps_setDestroy(GraphOps *ops, GraphDestroyFn fnPtr);

#define Graph_addVertex(graph, val)                                          \
    ({                                                                       \
        auto _vex_g_av = (val);                                              \
        Graph_addVertexBytes((graph), &_vex_g_av, sizeof(_vex_g_av));        \
    })

#define Graph_setVertex(graph, id, val)                                      \
    ({                                                                       \
        auto _vex_g_sv = (val);                                              \
        Graph_setVertexBytes((graph), (id), &_vex_g_sv, sizeof(_vex_g_sv));  \
    })

#define Graph_vertexAs(graph, id, V)                                         \
    ({                                                                       \
        typeof_unqual(V) _vex_g_va_out;                                      \
        memset(&_vex_g_va_out, 0, sizeof(_vex_g_va_out));                    \
        (discard) Graph_vertexCopyOut((graph), (id), &_vex_g_va_out,         \
                                      sizeof(_vex_g_va_out), #V);            \
        _vex_g_va_out;                                                       \
    })

#define Graph_addEdge(graph, from, to, val)                                  \
    ({                                                                       \
        auto _vex_g_ae = (val);                                              \
        Graph_addEdgeBytes((graph), (from), (to),                            \
                           &_vex_g_ae, sizeof(_vex_g_ae));                   \
    })

#define Graph_addBiEdge(graph, a, b, val)                                    \
    ({                                                                       \
        auto _vex_g_be = (val);                                              \
        Graph_addBiEdgeBytes((graph), (a), (b),                              \
                             &_vex_g_be, sizeof(_vex_g_be));                 \
    })

#define Graph_edgeAs(graph, from, to, E)                                     \
    ({                                                                       \
        typeof_unqual(E) _vex_g_ea_out;                                      \
        memset(&_vex_g_ea_out, 0, sizeof(_vex_g_ea_out));                    \
        (discard) Graph_edgeCopyOut((graph), (from), (to), &_vex_g_ea_out,   \
                                    sizeof(_vex_g_ea_out), #E);              \
        _vex_g_ea_out;                                                       \
    })

#endif

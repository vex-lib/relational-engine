#ifndef STRUCT_LINKED_GRAPH_H
#define STRUCT_LINKED_GRAPH_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/graph.h"

/**
 * struct/linked_graph.h, pointer-stable linked-node Graph implementation.
 *
 * Vertices live in immovable 32-byte-aligned LinkedGraphVertex nodes and each
 * vertex owns a singly-linked chain of 32-byte-aligned LinkedGraphEdge nodes.
 * A flat pointer directory indexed by vertex id provides O(1) vertex lookup
 * while guaranteeing stable vertex and edge payload pointers across growth.
 *
 * Graph *g = LinkedGraph(City);
 * Graph *w = LinkedGraph(City, Road);
 * Graph *a = LinkedGraph(City, Road, myArena);
 */

#define LINKED_GRAPH_DEFAULT_INDEX_CAPACITY 16u

/**
 * 32-byte linked outgoing edge header preceding inline edge payload bytes.
 *
 * struct LinkedGraphEdge *next - next outgoing edge of the source vertex
 * u32 to                       - target vertex id
 * u32 pad0                     - alignment padding
 * u64 pad1                     - alignment padding
 * u64 pad2                     - alignment padding
 */
;;CLASSNAME("LinkedGraphEdge")
typedef struct LinkedGraphEdge {
    struct LinkedGraphEdge *next;
    u32 to;
    u32 pad0;
    u64 pad1;
    u64 pad2;
} LinkedGraphEdge;

_Static_assert(sizeof(LinkedGraphEdge) == 32u, "LinkedGraphEdge header must be 32 bytes");

/**
 * 32-byte linked vertex header preceding inline vertex payload bytes.
 *
 * struct LinkedGraphVertex *next - next vertex in allocation order
 * LinkedGraphEdge *edges         - head of outgoing linked edge chain
 * u32 id                         - dense vertex id
 * u32 outDegree                  - number of outgoing edges
 * u64 pad0                       - alignment padding to 32 bytes
 */
;;CLASSNAME("LinkedGraphVertex")
typedef struct LinkedGraphVertex {
    struct LinkedGraphVertex *next;
    LinkedGraphEdge *edges;
    u32 id;
    u32 outDegree;
    u64 pad0;
} LinkedGraphVertex;

_Static_assert(sizeof(LinkedGraphVertex) == 32u, "LinkedGraphVertex header must be 32 bytes");

/**
 * Linked-node Graph embedding Graph at offset zero.
 *
 * Graph base                 - polymorphic Graph header
 * LinkedGraphVertex **index  - growable vertex pointer directory (O(1) by id)
 * LinkedGraphVertex *head    - oldest vertex node
 * LinkedGraphVertex *tail    - newest vertex node
 * bytes vertexNodeBytes      - total allocation bytes per vertex node
 * bytes edgeNodeBytes        - total allocation bytes per edge node
 * bytes edgeTotal            - total active directed edges
 */
;;CLASSNAME("LinkedGraph")
typedef struct LinkedGraph {
    Graph base;
    LinkedGraphVertex **index;
    LinkedGraphVertex *head;
    LinkedGraphVertex *tail;
    bytes vertexNodeBytes;
    bytes edgeNodeBytes;
    bytes edgeTotal;
} LinkedGraph;

Graph *LinkedGraph_newLayout(ElementLayout vertexLayout, ElementLayout edgeLayout,
                             MemoryArena *arena);

#define LinkedGraph_ctor_1(V)          LinkedGraph_newLayout(ElementLayout_of(V), ElementLayout_of(f32), Re_arena())
#define LinkedGraph_ctor_2(V, E)       LinkedGraph_newLayout(ElementLayout_of(V), ElementLayout_of(E), Re_arena())
#define LinkedGraph_ctor_3(V, E, arena) LinkedGraph_newLayout(ElementLayout_of(V), ElementLayout_of(E), (arena))
#define LinkedGraph(...)               CONSTRUCTOR_DISPATCH(LinkedGraph_ctor, __VA_ARGS__)

bool LinkedGraph_isLinkedGraph(const Graph *self);
u32 LinkedGraph_addVertexBytes(Graph *self, const_any vertex, bytes vertexBytes);
any LinkedGraph_emplaceVertex(Graph *self, u32 *outId);
any LinkedGraph_vertex(const Graph *self, u32 id);
bool LinkedGraph_setVertexBytes(Graph *self, u32 id, const_any vertex, bytes vertexBytes);
bool LinkedGraph_addEdgeBytes(Graph *self, u32 from, u32 to, const_any edge, bytes edgeBytes);
any LinkedGraph_edge(const Graph *self, u32 from, u32 to);
bool LinkedGraph_removeEdge(Graph *self, u32 from, u32 to);
bytes LinkedGraph_outDegree(const Graph *self, u32 from);
fn LinkedGraph_forEachEdge(Graph *self, u32 from, GraphEdgeVisitFn visit, any userdata);
List *LinkedGraph_neighbors(const Graph *self, u32 from);
fn LinkedGraph_clear(Graph *self);
fn LinkedGraph_free(Graph *self);

#endif

#ifndef STRUCT_ARRAY_GRAPH_H
#define STRUCT_ARRAY_GRAPH_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/graph.h"

/**
 * struct/array_graph.h, contiguous DOD array-backed Graph (default Graph).
 *
 * Vertices live inline inside one flat arena buffer and each vertex owns a
 * growable flat adjacency array of [u32 to][pad][edgePayload] records.
 *
 * Graph *g = ArrayGraph(City);
 * Graph *w = ArrayGraph(City, Road);
 * Graph *a = ArrayGraph(City, Road, myArena);
 */

#define ARRAY_GRAPH_DEFAULT_CAPACITY 16u

/**
 * Flat growable outgoing edge list owned by one vertex inside ArrayGraph.
 *
 * u8 *edges    - flat buffer of aligned [u32 to][pad][edgePayload] slots
 * u32 count    - active outgoing edges
 * u32 capacity - allocated outgoing edge slots
 */
;;CLASSNAME("ArrayGraphAdj")
typedef struct ArrayGraphAdj {
    u8 *edges;
    u32 count;
    u32 capacity;
} ArrayGraphAdj;

/**
 * Array-backed Graph embedding Graph at offset zero.
 *
 * Graph base           - polymorphic Graph header and vertex buffer
 * ArrayGraphAdj *adj   - flat per-vertex adjacency buckets (length = capacity)
 * bytes edgeOffset     - aligned byte offset of edge payload in one edge slot
 * bytes edgeSlotStride - aligned byte width of one edge slot
 * bytes edgeTotal      - total active directed edges across all vertices
 */
;;CLASSNAME("ArrayGraph")
typedef struct ArrayGraph {
    Graph base;
    ArrayGraphAdj *adj;
    bytes edgeOffset;
    bytes edgeSlotStride;
    bytes edgeTotal;
} ArrayGraph;

Graph *ArrayGraph_newLayout(ElementLayout vertexLayout, ElementLayout edgeLayout,
                            bytes initialVertices, MemoryArena *arena);

#define ArrayGraph_ctor_1(V)          ArrayGraph_newLayout(ElementLayout_of(V), ElementLayout_of(f32), ARRAY_GRAPH_DEFAULT_CAPACITY, Re_arena())
#define ArrayGraph_ctor_2(V, E)       ArrayGraph_newLayout(ElementLayout_of(V), ElementLayout_of(E), ARRAY_GRAPH_DEFAULT_CAPACITY, Re_arena())
#define ArrayGraph_ctor_3(V, E, arena) ArrayGraph_newLayout(ElementLayout_of(V), ElementLayout_of(E), ARRAY_GRAPH_DEFAULT_CAPACITY, (arena))
#define ArrayGraph(...)               CONSTRUCTOR_DISPATCH(ArrayGraph_ctor, __VA_ARGS__)

#define Graph(...)                    ArrayGraph(__VA_ARGS__)

bool ArrayGraph_isArrayGraph(const Graph *self);
u32 ArrayGraph_addVertexBytes(Graph *self, const_any vertex, bytes vertexBytes);
any ArrayGraph_emplaceVertex(Graph *self, u32 *outId);
any ArrayGraph_vertex(const Graph *self, u32 id);
bool ArrayGraph_setVertexBytes(Graph *self, u32 id, const_any vertex, bytes vertexBytes);
bool ArrayGraph_addEdgeBytes(Graph *self, u32 from, u32 to, const_any edge, bytes edgeBytes);
any ArrayGraph_edge(const Graph *self, u32 from, u32 to);
bool ArrayGraph_removeEdge(Graph *self, u32 from, u32 to);
bytes ArrayGraph_outDegree(const Graph *self, u32 from);
fn ArrayGraph_forEachEdge(Graph *self, u32 from, GraphEdgeVisitFn visit, any userdata);
List *ArrayGraph_neighbors(const Graph *self, u32 from);
fn ArrayGraph_clear(Graph *self);
fn ArrayGraph_free(Graph *self);

#endif

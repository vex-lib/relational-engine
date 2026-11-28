#include "vex.h"
#include <stdckdint.h>

#include "atomic/atomic.h"
#include "nio/mem.h"
#include "struct/array_graph.h"
#include "struct/array_list.h"
#include "struct/exhaustion.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ArrayGraph
 * ============================================================================
 * Contiguous, data-oriented Graph implementation (default Graph backing) owned
 * by Relational Engine (lifetime tier). Vertex payloads live in a single flat
 * 32-byte-aligned MemoryArena buffer indexed by dense u32 vertex ids, and each
 * vertex owns a growable contiguous adjacency array storing [u32 to][pad]
 * [edgePayload] records for cache-linear graph traversals.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ArrayGraph (struct/array_graph.c)
 * ============================================================================
 * Flat DOD Graph with contiguous vertex array and flat adjacency arrays.
 *
 * --CONSTANTS----------------------------
 * public   const u32 ARRAY_GRAPH_DEFAULT_CAPACITY  default vertex capacity (16)
 *
 * --FIELDS-------------------------------
 * public   Graph          base                     embedded Graph header, 1:1
 * private  ArrayGraphAdj *adj                      owns per-vertex buckets, 1:N
 * private  bytes          edgeOffset               aligned edge payload offset
 * private  bytes          edgeSlotStride           aligned edge record stride
 * private  bytes          edgeTotal                active directed edge count
 *
 * --CONSTRUCTORS-------------------------
 * public   ArrayGraph_newLayout(...)               constructs ArrayGraph
 *
 * --FUNCTIONS----------------------------
 * public   ArrayGraph_isArrayGraph(...)            checks GRAPH_KIND_ARRAY
 * public   ArrayGraph_addVertexBytes / emplaceVertex / vertex / setVertexBytes
 * public   ArrayGraph_addEdgeBytes / edge / removeEdge / outDegree
 * public   ArrayGraph_forEachEdge / neighbors / clear / free
 * private  arrayGraphOps(...)                      lazy thread-safe vtable
 * private  arrayGraphGrowVertices(...)             grows vertex + adj arrays
 * private  arrayGraphGrowAdj(...)                  grows one vertex edge array
 *
 * --GETTERS------------------------------
 * private  Ag_* / AgAdj_* / AgColl_*               internal field readers
 *
 * --SETTERS------------------------------
 * private  Ag_set* / AgAdj_set* / AgColl_set*      internal field writers
 * ============================================================================
 */

static GraphOps s_arrayGraphOps;
static SpinLock s_arrayGraphOpsLock = SPIN_LOCK_INIT;
static atomic_bool s_arrayGraphOpsReady = false;

;;GETTER
static inline ArrayGraphAdj *Ag_adj(const ArrayGraph *ag)
{
    return (*ag).adj;
}

;;GETTER
static inline bytes Ag_edgeOffset(const ArrayGraph *ag)
{
    return (*ag).edgeOffset;
}

;;GETTER
static inline bytes Ag_edgeSlotStride(const ArrayGraph *ag)
{
    return (*ag).edgeSlotStride;
}

;;GETTER
static inline bytes Ag_edgeTotal(const ArrayGraph *ag)
{
    return (*ag).edgeTotal;
}

;;SETTER
static inline fn Ag_setAdj(ArrayGraph *ag, ArrayGraphAdj *adj)
{
    (*ag).adj = adj;
}

;;SETTER
static inline fn Ag_setEdgeOffset(ArrayGraph *ag, bytes offset)
{
    (*ag).edgeOffset = offset;
}

;;SETTER
static inline fn Ag_setEdgeSlotStride(ArrayGraph *ag, bytes stride)
{
    (*ag).edgeSlotStride = stride;
}

;;SETTER
static inline fn Ag_setEdgeTotal(ArrayGraph *ag, bytes total)
{
    (*ag).edgeTotal = total;
}

;;GETTER
static inline u8 *AgAdj_edges(const ArrayGraphAdj *b)
{
    return (*b).edges;
}

;;GETTER
static inline u32 AgAdj_count(const ArrayGraphAdj *b)
{
    return (*b).count;
}

;;GETTER
static inline u32 AgAdj_capacity(const ArrayGraphAdj *b)
{
    return (*b).capacity;
}

;;SETTER
static inline fn AgAdj_setEdges(ArrayGraphAdj *b, u8 *edges)
{
    (*b).edges = edges;
}

;;SETTER
static inline fn AgAdj_setCount(ArrayGraphAdj *b, u32 count)
{
    (*b).count = count;
}

;;SETTER
static inline fn AgAdj_setCapacity(ArrayGraphAdj *b, u32 cap)
{
    (*b).capacity = cap;
}

;;GETTER
static inline u32 AgColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;GETTER
static inline u32 AgColl_capacity(const Collection *c)
{
    return (*c).capacity;
}

;;GETTER
static inline u8 *AgColl_data(const Collection *c)
{
    return (*c).data;
}

;;SETTER
static inline fn AgColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn AgColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;SETTER
static inline fn AgColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

static inline u8 *agEdgeSlotAt(u8 *edges, bytes slotStride, bytes idx)
{
    return edges + (idx * slotStride);
}

static inline u32 agEdgeTo(const u8 *slot)
{
    u32 to = 0;
    memcpy(&to, slot, sizeof(u32));

    return to;
}

static inline fn agEdgeWriteTo(u8 *slot, u32 to)
{
    memcpy(slot, &to, sizeof(u32));
}

static inline u8 *agEdgePayload(u8 *slot, bytes edgeOffset)
{
    return slot + edgeOffset;
}

static bool arrayGraphGrowVertices(ArrayGraph *ag, bytes minCap)
{
    Graph *self = (Graph*) ag;
    Collection *c = (Collection*) ag;
    u32 oldCap = AgColl_capacity(c);
    bytes newCap = oldCap == 0 ? ARRAY_GRAPH_DEFAULT_CAPACITY : (bytes) oldCap;

    while(newCap < minCap) {
        if(newCap > ((bytes) (UINT32_MAX - 1u) >> 1u)) {
            if(minCap < (bytes) UINT32_MAX) {
                newCap = minCap;
                break;
            }

            return Collection_reportExhaustion(c, 0);
        }

        newCap <<= 1u;
    }

    if(newCap >= (bytes) UINT32_MAX)
        return Collection_reportExhaustion(c, 0);

    bytes vStride = Graph_vertexStride(self);
    bytes vBytes = 0;
    bytes adjBytes = 0;

    if(ckd_mul(&vBytes, newCap, vStride)
       || ckd_mul(&adjBytes, newCap, sizeof(ArrayGraphAdj))) {
        return Collection_reportExhaustion(c, 0);
    }

    MemoryArena *arena = Graph_arena(self);
    u8 *newVerts = (u8*) Memory_alloc(TYPE_BYTE_ARRAY, arena, vBytes);

    if(!newVerts)
        return Collection_reportExhaustion(c, vBytes);

    ArrayGraphAdj *newAdj = (ArrayGraphAdj*) Memory_alloc(TYPE_BYTE_ARRAY, arena, adjBytes);

    if(!newAdj) {
        MemoryArena_free(arena, newVerts);

        return Collection_reportExhaustion(c, adjBytes);
    }

    memset(newAdj, 0, adjBytes);

    u8 *oldVerts = AgColl_data(c);
    ArrayGraphAdj *oldAdj = Ag_adj(ag);
    u32 active = AgColl_activeCount(c);

    if(oldVerts && active > 0)
        memcpy(newVerts, oldVerts, (bytes) active * vStride);

    if(oldAdj && active > 0)
        memcpy(newAdj, oldAdj, (bytes) active * sizeof(ArrayGraphAdj));

    if(oldVerts)
        MemoryArena_free(arena, oldVerts);

    if(oldAdj)
        MemoryArena_free(arena, oldAdj);

    AgColl_setData(c, newVerts);
    AgColl_setCapacity(c, (u32) newCap);
    Ag_setAdj(ag, newAdj);

    return true;
}

static bool arrayGraphGrowAdj(ArrayGraph *ag, ArrayGraphAdj *bucket, bytes minCap)
{
    Graph *self = (Graph*) ag;
    Collection *c = (Collection*) ag;
    u32 oldCap = AgAdj_capacity(bucket);
    bytes newCap = oldCap == 0 ? 4u : (bytes) oldCap;

    while(newCap < minCap) {
        if(newCap > ((bytes) UINT32_MAX >> 1u))
            return Collection_reportExhaustion(c, 0);

        newCap <<= 1u;
    }

    bytes slotStride = Ag_edgeSlotStride(ag);
    bytes allocBytes = 0;

    if(ckd_mul(&allocBytes, newCap, slotStride))
        return Collection_reportExhaustion(c, 0);

    MemoryArena *arena = Graph_arena(self);
    u8 *newEdges = (u8*) Memory_alloc(TYPE_BYTE_ARRAY, arena, allocBytes);

    if(!newEdges)
        return Collection_reportExhaustion(c, allocBytes);

    u8 *oldEdges = AgAdj_edges(bucket);
    u32 active = AgAdj_count(bucket);

    if(oldEdges && active > 0)
        memcpy(newEdges, oldEdges, (bytes) active * slotStride);

    if(oldEdges)
        MemoryArena_free(arena, oldEdges);

    AgAdj_setEdges(bucket, newEdges);
    AgAdj_setCapacity(bucket, (u32) newCap);

    return true;
}

static bytes arrayGraphVertexCount(const Graph *self)
{
    if(!self)
        return 0;

    return (bytes) AgColl_activeCount((const Collection*) self);
}

static bytes arrayGraphEdgeCount(const Graph *self)
{
    if(!ArrayGraph_isArrayGraph(self))
        return 0;

    return Ag_edgeTotal((const ArrayGraph*) self);
}

static const GraphOps *arrayGraphOps()
{
    if(atomic_load_explicit(&s_arrayGraphOpsReady, memory_order_acquire))
        return &s_arrayGraphOps;

    SpinLock_lock(&s_arrayGraphOpsLock);

    if(!atomic_load_explicit(&s_arrayGraphOpsReady, memory_order_relaxed)) {
        GraphOps_setKind(&s_arrayGraphOps, GRAPH_KIND_ARRAY);
        GraphOps_setKindName(&s_arrayGraphOps, "ArrayGraph");
        GraphOps_setAddVertexBytes(&s_arrayGraphOps, ArrayGraph_addVertexBytes);
        GraphOps_setEmplaceVertex(&s_arrayGraphOps, ArrayGraph_emplaceVertex);
        GraphOps_setVertex(&s_arrayGraphOps, ArrayGraph_vertex);
        GraphOps_setWriteVertexBytes(&s_arrayGraphOps, ArrayGraph_setVertexBytes);
        GraphOps_setAddEdgeBytes(&s_arrayGraphOps, ArrayGraph_addEdgeBytes);
        GraphOps_setEdge(&s_arrayGraphOps, ArrayGraph_edge);
        GraphOps_setRemoveEdge(&s_arrayGraphOps, ArrayGraph_removeEdge);
        GraphOps_setOutDegree(&s_arrayGraphOps, ArrayGraph_outDegree);
        GraphOps_setForEachEdge(&s_arrayGraphOps, ArrayGraph_forEachEdge);
        GraphOps_setNeighbors(&s_arrayGraphOps, ArrayGraph_neighbors);
        GraphOps_setClear(&s_arrayGraphOps, ArrayGraph_clear);
        GraphOps_setVertexCount(&s_arrayGraphOps, arrayGraphVertexCount);
        GraphOps_setEdgeCount(&s_arrayGraphOps, arrayGraphEdgeCount);
        GraphOps_setDestroy(&s_arrayGraphOps, ArrayGraph_free);
        atomic_store_explicit(&s_arrayGraphOpsReady, true, memory_order_release);
    }

    SpinLock_unlock(&s_arrayGraphOpsLock);

    return &s_arrayGraphOps;
}

;;CONSTRUCTOR
Graph *ArrayGraph_newLayout(ElementLayout vertexLayout, ElementLayout edgeLayout,
                            bytes initialVertices, MemoryArena *arena)
{
    if(!ElementLayout_isValid(vertexLayout) || !ElementLayout_isValid(edgeLayout)) {
        THROW("ArrayGraph: invalid layout for <%s, %s>",
              ElementLayout_typeName(vertexLayout),
              ElementLayout_typeName(edgeLayout));

        return nullptr;
    }

    if(initialVertices >= (bytes) UINT32_MAX) {
        THROW("ArrayGraph: initial vertex capacity %zu overflows u32", initialVertices);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    bytes edgeAlign = ElementLayout_alignment(edgeLayout);
    bytes slotAlign = edgeAlign > sizeof(u32) ? edgeAlign : sizeof(u32);
    bytes edgeOffset = ElementLayout_alignUp(sizeof(u32), edgeAlign);
    bytes edgeSlotStride = ElementLayout_alignUp(edgeOffset + ElementLayout_stride(edgeLayout), slotAlign);

    ArrayGraph *ag = (ArrayGraph*) Memory_alloc(TYPE_ARRAY_GRAPH, targetArena, sizeof(ArrayGraph));

    if(!ag)
        return nullptr;

    Graph *base = (Graph*) ag;

    if(!Graph_initBase(base, TYPE_ARRAY_GRAPH, arrayGraphOps(),
                       vertexLayout, edgeLayout, targetArena)) {
        MemoryArena_free(targetArena, ag);

        return nullptr;
    }

    Ag_setAdj(ag, nullptr);
    Ag_setEdgeOffset(ag, edgeOffset);
    Ag_setEdgeSlotStride(ag, edgeSlotStride);
    Ag_setEdgeTotal(ag, 0);

    if(initialVertices > 0) {
        if(!arrayGraphGrowVertices(ag, initialVertices)) {
            MemoryArena_free(targetArena, ag);

            return nullptr;
        }
    }

    return base;
}

bool ArrayGraph_isArrayGraph(const Graph *self)
{
    return Graph_kind(self) == GRAPH_KIND_ARRAY;
}

any ArrayGraph_emplaceVertex(Graph *self, u32 *outId)
{
    if(outId)
        *outId = GRAPH_INVALID_VERTEX;

    if(!ArrayGraph_isArrayGraph(self))
        return nullptr;

    ArrayGraph *ag = (ArrayGraph*) self;
    Collection *c = (Collection*) self;
    u32 active = AgColl_activeCount(c);

    if(active >= UINT32_MAX - 1u) {
        (discard) Collection_reportExhaustion(c, 0);

        return nullptr;
    }

    if(active >= AgColl_capacity(c)) {
        if(!arrayGraphGrowVertices(ag, (bytes) active + 1u))
            return nullptr;
    }

    bytes vStride = Graph_vertexStride(self);
    u8 *slot = AgColl_data(c) + ((bytes) active * vStride);
    memset(slot, 0, vStride);

    ArrayGraphAdj *bucket = &Ag_adj(ag)[active];
    AgAdj_setEdges(bucket, nullptr);
    AgAdj_setCount(bucket, 0);
    AgAdj_setCapacity(bucket, 0);

    AgColl_setActiveCount(c, active + 1u);

    if(outId)
        *outId = active;

    return slot;
}

u32 ArrayGraph_addVertexBytes(Graph *self, const_any vertex, bytes vertexBytes)
{
    if(!ArrayGraph_isArrayGraph(self) || !vertex || vertexBytes != Graph_vertexStride(self))
        return GRAPH_INVALID_VERTEX;

    u32 id = GRAPH_INVALID_VERTEX;
    any slot = ArrayGraph_emplaceVertex(self, &id);

    if(!slot)
        return GRAPH_INVALID_VERTEX;

    memcpy(slot, vertex, vertexBytes);

    return id;
}

;;HOTCODE
any ArrayGraph_vertex(const Graph *self, u32 id)
{
    if(!ArrayGraph_isArrayGraph(self))
        return nullptr;

    const Collection *c = (const Collection*) self;

    if(id >= AgColl_activeCount(c))
        return nullptr;

    u8 *data = AgColl_data(c);

    if(!data)
        return nullptr;

    return data + ((bytes) id * Graph_vertexStride(self));
}

bool ArrayGraph_setVertexBytes(Graph *self, u32 id, const_any vertex, bytes vertexBytes)
{
    if(!ArrayGraph_isArrayGraph(self) || !vertex || vertexBytes != Graph_vertexStride(self))
        return false;

    any slot = ArrayGraph_vertex(self, id);

    if(!slot)
        return false;

    memcpy(slot, vertex, vertexBytes);

    return true;
}

bool ArrayGraph_addEdgeBytes(Graph *self, u32 from, u32 to, const_any edge, bytes edgeBytes)
{
    if(!ArrayGraph_isArrayGraph(self) || !edge || edgeBytes != Graph_edgeStride(self))
        return false;

    ArrayGraph *ag = (ArrayGraph*) self;
    Collection *c = (Collection*) self;
    u32 active = AgColl_activeCount(c);

    if(from >= active || to >= active)
        return false;

    ArrayGraphAdj *bucket = &Ag_adj(ag)[from];
    u8 *edges = AgAdj_edges(bucket);
    u32 deg = AgAdj_count(bucket);
    bytes slotStride = Ag_edgeSlotStride(ag);
    bytes edgeOffset = Ag_edgeOffset(ag);

    for(u32 i = 0; i < deg; i++) {
        u8 *slot = agEdgeSlotAt(edges, slotStride, i);

        if(agEdgeTo(slot) == to) {
            memcpy(agEdgePayload(slot, edgeOffset), edge, edgeBytes);

            return true;
        }
    }

    if(deg >= AgAdj_capacity(bucket)) {
        if(!arrayGraphGrowAdj(ag, bucket, (bytes) deg + 1u))
            return false;

        edges = AgAdj_edges(bucket);
    }

    u8 *slot = agEdgeSlotAt(edges, slotStride, deg);
    memset(slot, 0, slotStride);
    agEdgeWriteTo(slot, to);
    memcpy(agEdgePayload(slot, edgeOffset), edge, edgeBytes);
    AgAdj_setCount(bucket, deg + 1u);
    Ag_setEdgeTotal(ag, Ag_edgeTotal(ag) + 1u);

    return true;
}

;;HOTCODE
any ArrayGraph_edge(const Graph *self, u32 from, u32 to)
{
    if(!ArrayGraph_isArrayGraph(self))
        return nullptr;

    const ArrayGraph *ag = (const ArrayGraph*) self;
    const Collection *c = (const Collection*) self;
    u32 active = AgColl_activeCount(c);

    if(from >= active || to >= active)
        return nullptr;

    const ArrayGraphAdj *bucket = &Ag_adj(ag)[from];
    u8 *edges = AgAdj_edges(bucket);
    u32 deg = AgAdj_count(bucket);
    bytes slotStride = Ag_edgeSlotStride(ag);
    bytes edgeOffset = Ag_edgeOffset(ag);

    for(u32 i = 0; i < deg; i++) {
        u8 *slot = agEdgeSlotAt(edges, slotStride, i);

        if(agEdgeTo(slot) == to)
            return agEdgePayload(slot, edgeOffset);
    }

    return nullptr;
}

bool ArrayGraph_removeEdge(Graph *self, u32 from, u32 to)
{
    if(!ArrayGraph_isArrayGraph(self))
        return false;

    ArrayGraph *ag = (ArrayGraph*) self;
    Collection *c = (Collection*) self;
    u32 active = AgColl_activeCount(c);

    if(from >= active || to >= active)
        return false;

    ArrayGraphAdj *bucket = &Ag_adj(ag)[from];
    u8 *edges = AgAdj_edges(bucket);
    u32 deg = AgAdj_count(bucket);
    bytes slotStride = Ag_edgeSlotStride(ag);

    for(u32 i = 0; i < deg; i++) {
        u8 *slot = agEdgeSlotAt(edges, slotStride, i);

        if(agEdgeTo(slot) == to) {
            u32 lastIdx = deg - 1u;

            if(i < lastIdx) {
                memcpy(slot, agEdgeSlotAt(edges, slotStride, lastIdx), slotStride);
            }

            AgAdj_setCount(bucket, lastIdx);

            if(Ag_edgeTotal(ag) > 0)
                Ag_setEdgeTotal(ag, Ag_edgeTotal(ag) - 1u);

            return true;
        }
    }

    return false;
}

bytes ArrayGraph_outDegree(const Graph *self, u32 from)
{
    if(!ArrayGraph_isArrayGraph(self))
        return 0;

    const ArrayGraph *ag = (const ArrayGraph*) self;
    const Collection *c = (const Collection*) self;

    if(from >= AgColl_activeCount(c))
        return 0;

    return (bytes) AgAdj_count(&Ag_adj(ag)[from]);
}

fn ArrayGraph_forEachEdge(Graph *self, u32 from, GraphEdgeVisitFn visit, any userdata)
{
    if(!ArrayGraph_isArrayGraph(self) || !visit)
        return;

    ArrayGraph *ag = (ArrayGraph*) self;
    Collection *c = (Collection*) self;

    if(from >= AgColl_activeCount(c))
        return;

    ArrayGraphAdj *bucket = &Ag_adj(ag)[from];
    u8 *edges = AgAdj_edges(bucket);
    u32 deg = AgAdj_count(bucket);
    bytes slotStride = Ag_edgeSlotStride(ag);
    bytes edgeOffset = Ag_edgeOffset(ag);

    for(u32 i = 0; i < deg; i++) {
        u8 *slot = agEdgeSlotAt(edges, slotStride, i);
        visit(from, agEdgeTo(slot), agEdgePayload(slot, edgeOffset), i, userdata);
    }
}

List *ArrayGraph_neighbors(const Graph *self, u32 from)
{
    if(!ArrayGraph_isArrayGraph(self))
        return nullptr;

    const ArrayGraph *ag = (const ArrayGraph*) self;
    const Collection *c = (const Collection*) self;

    if(from >= AgColl_activeCount(c))
        return nullptr;

    const ArrayGraphAdj *bucket = &Ag_adj(ag)[from];
    u32 deg = AgAdj_count(bucket);
    List *out = ArrayList_newLayout(ElementLayout_of(u32), deg < 4u ? 4u : deg, Graph_arena(self));

    if(!out)
        return nullptr;

    u8 *edges = AgAdj_edges(bucket);
    bytes slotStride = Ag_edgeSlotStride(ag);

    for(u32 i = 0; i < deg; i++) {
        u32 to = agEdgeTo(agEdgeSlotAt(edges, slotStride, i));

        if(!List_addBytes(out, &to, sizeof(u32))) {
            List_free(out);

            return nullptr;
        }
    }

    return out;
}

fn ArrayGraph_clear(Graph *self)
{
    if(!ArrayGraph_isArrayGraph(self))
        return;

    ArrayGraph *ag = (ArrayGraph*) self;
    Collection *c = (Collection*) self;
    MemoryArena *arena = Graph_arena(self);
    ArrayGraphAdj *adj = Ag_adj(ag);
    u32 active = AgColl_activeCount(c);

    if(adj) {
        for(u32 i = 0; i < active; i++) {
            u8 *edges = AgAdj_edges(&adj[i]);

            if(edges) {
                MemoryArena_free(arena, edges);
                AgAdj_setEdges(&adj[i], nullptr);
            }

            AgAdj_setCount(&adj[i], 0);
            AgAdj_setCapacity(&adj[i], 0);
        }
    }

    AgColl_setActiveCount(c, 0);
    Ag_setEdgeTotal(ag, 0);
}

;;DECONSTRUCT
fn ArrayGraph_free(Graph *self)
{
    if(!ArrayGraph_isArrayGraph(self))
        return;

    ArrayGraph *ag = (ArrayGraph*) self;
    Collection *c = (Collection*) self;
    MemoryArena *arena = Graph_arena(self);
    ArrayGraph_clear(self);

    ArrayGraphAdj *adj = Ag_adj(ag);

    if(adj) {
        MemoryArena_free(arena, adj);
        Ag_setAdj(ag, nullptr);
    }

    u8 *verts = AgColl_data(c);

    if(verts) {
        MemoryArena_free(arena, verts);
        AgColl_setData(c, nullptr);
    }

    MemoryArena_free(arena, self);
}

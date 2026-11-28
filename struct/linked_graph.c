#include "vex.h"
#include <stdckdint.h>

#include "atomic/atomic.h"
#include "nio/mem.h"
#include "struct/array_list.h"
#include "struct/exhaustion.h"
#include "struct/linked_graph.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: LinkedGraph
 * ============================================================================
 * Pointer-stable linked-node Graph implementation owned by Relational Engine
 * (lifetime tier). Each vertex is an immovable 32-byte-aligned arena node
 * carrying inline vertex bytes and a linked adjacency list of immovable
 * 32-byte-aligned edge nodes. A growable vertex pointer directory gives O(1)
 * id lookup while keeping all vertex and edge payload pointers valid until
 * removed or cleared.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: LinkedGraph (struct/linked_graph.c)
 * ============================================================================
 * Linked-node Graph with pointer stability and O(1) vertex id index.
 *
 * --FIELDS-------------------------------
 * public   Graph               base            embedded Graph header, 1:1
 * private  LinkedGraphVertex **index           owns pointer directory, 1:N
 * private  LinkedGraphVertex  *head            owns vertex chain, 1:N
 * private  LinkedGraphVertex  *tail            borrows newest vertex, 1:1
 * private  bytes               vertexNodeBytes bytes per vertex node
 * private  bytes               edgeNodeBytes   bytes per edge node
 * private  bytes               edgeTotal       active directed edge count
 *
 * --CONSTRUCTORS-------------------------
 * public   LinkedGraph_newLayout(...)          constructs LinkedGraph in arena
 *
 * --FUNCTIONS----------------------------
 * public   LinkedGraph_isLinkedGraph(...)      checks GRAPH_KIND_LINKED
 * public   LinkedGraph_addVertexBytes / emplaceVertex / vertex / setVertexBytes
 * public   LinkedGraph_addEdgeBytes / edge / removeEdge / outDegree
 * public   LinkedGraph_forEachEdge / neighbors / clear / free
 * private  linkedGraphOps(...)                 lazy thread-safe vtable
 * private  linkedGraphGrowIndex(...)           grows vertex pointer directory
 *
 * --GETTERS------------------------------
 * private  LgEdge_* / LgVert_* / Lg_* / LgColl_*
 *
 * --SETTERS------------------------------
 * private  LgEdge_set* / LgVert_set* / Lg_set* / LgColl_set*
 * ============================================================================
 */

static GraphOps s_linkedGraphOps;
static SpinLock s_linkedGraphOpsLock = SPIN_LOCK_INIT;
static atomic_bool s_linkedGraphOpsReady = false;

;;GETTER
static inline LinkedGraphEdge *LgEdge_next(const LinkedGraphEdge *e)
{
    return (*e).next;
}

;;GETTER
static inline u32 LgEdge_to(const LinkedGraphEdge *e)
{
    return (*e).to;
}

;;SETTER
static inline fn LgEdge_setNext(LinkedGraphEdge *e, LinkedGraphEdge *next)
{
    (*e).next = next;
}

;;SETTER
static inline fn LgEdge_setTo(LinkedGraphEdge *e, u32 to)
{
    (*e).to = to;
}

;;SETTER
static inline fn LgEdge_setPads(LinkedGraphEdge *e)
{
    (*e).pad0 = 0;
    (*e).pad1 = 0;
    (*e).pad2 = 0;
}

;;GETTER
static inline LinkedGraphVertex *LgVert_next(const LinkedGraphVertex *v)
{
    return (*v).next;
}

;;GETTER
static inline LinkedGraphEdge *LgVert_edges(const LinkedGraphVertex *v)
{
    return (*v).edges;
}

;;GETTER
static inline u32 LgVert_outDegree(const LinkedGraphVertex *v)
{
    return (*v).outDegree;
}

;;SETTER
static inline fn LgVert_setNext(LinkedGraphVertex *v, LinkedGraphVertex *next)
{
    (*v).next = next;
}

;;SETTER
static inline fn LgVert_setEdges(LinkedGraphVertex *v, LinkedGraphEdge *edges)
{
    (*v).edges = edges;
}

;;SETTER
static inline fn LgVert_setId(LinkedGraphVertex *v, u32 id)
{
    (*v).id = id;
}

;;SETTER
static inline fn LgVert_setOutDegree(LinkedGraphVertex *v, u32 deg)
{
    (*v).outDegree = deg;
}

;;SETTER
static inline fn LgVert_setPad(LinkedGraphVertex *v)
{
    (*v).pad0 = 0;
}

;;GETTER
static inline LinkedGraphVertex **Lg_index(const LinkedGraph *lg)
{
    return (*lg).index;
}

;;GETTER
static inline LinkedGraphVertex *Lg_head(const LinkedGraph *lg)
{
    return (*lg).head;
}

;;GETTER
static inline LinkedGraphVertex *Lg_tail(const LinkedGraph *lg)
{
    return (*lg).tail;
}

;;GETTER
static inline bytes Lg_vertexNodeBytes(const LinkedGraph *lg)
{
    return (*lg).vertexNodeBytes;
}

;;GETTER
static inline bytes Lg_edgeNodeBytes(const LinkedGraph *lg)
{
    return (*lg).edgeNodeBytes;
}

;;GETTER
static inline bytes Lg_edgeTotal(const LinkedGraph *lg)
{
    return (*lg).edgeTotal;
}

;;SETTER
static inline fn Lg_setIndex(LinkedGraph *lg, LinkedGraphVertex **index)
{
    (*lg).index = index;
}

;;SETTER
static inline fn Lg_setHead(LinkedGraph *lg, LinkedGraphVertex *head)
{
    (*lg).head = head;
}

;;SETTER
static inline fn Lg_setTail(LinkedGraph *lg, LinkedGraphVertex *tail)
{
    (*lg).tail = tail;
}

;;SETTER
static inline fn Lg_setVertexNodeBytes(LinkedGraph *lg, bytes n)
{
    (*lg).vertexNodeBytes = n;
}

;;SETTER
static inline fn Lg_setEdgeNodeBytes(LinkedGraph *lg, bytes n)
{
    (*lg).edgeNodeBytes = n;
}

;;SETTER
static inline fn Lg_setEdgeTotal(LinkedGraph *lg, bytes total)
{
    (*lg).edgeTotal = total;
}

;;GETTER
static inline u32 LgColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;GETTER
static inline u32 LgColl_capacity(const Collection *c)
{
    return (*c).capacity;
}

;;SETTER
static inline fn LgColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn LgColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

static inline u8 *lgVertPayload(LinkedGraphVertex *v)
{
    return ((u8*) v) + sizeof(LinkedGraphVertex);
}

static inline u8 *lgEdgePayload(LinkedGraphEdge *e)
{
    return ((u8*) e) + sizeof(LinkedGraphEdge);
}

static bool linkedGraphGrowIndex(LinkedGraph *lg, bytes minCap)
{
    Graph *self = (Graph*) lg;
    Collection *c = (Collection*) lg;
    u32 oldCap = LgColl_capacity(c);
    bytes newCap = oldCap == 0 ? LINKED_GRAPH_DEFAULT_INDEX_CAPACITY : (bytes) oldCap;

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

    bytes allocBytes = 0;

    if(ckd_mul(&allocBytes, newCap, sizeof(LinkedGraphVertex*)))
        return Collection_reportExhaustion(c, 0);

    MemoryArena *arena = Graph_arena(self);
    LinkedGraphVertex **newIdx = (LinkedGraphVertex**) Memory_alloc(
        TYPE_BYTE_ARRAY, arena, allocBytes);

    if(!newIdx)
        return Collection_reportExhaustion(c, allocBytes);

    LinkedGraphVertex **oldIdx = Lg_index(lg);
    u32 active = LgColl_activeCount(c);

    if(oldIdx && active > 0)
        memcpy(newIdx, oldIdx, (bytes) active * sizeof(LinkedGraphVertex*));

    if(oldIdx)
        MemoryArena_free(arena, oldIdx);

    Lg_setIndex(lg, newIdx);
    LgColl_setCapacity(c, (u32) newCap);

    return true;
}

static bytes linkedGraphVertexCount(const Graph *self)
{
    if(!self)
        return 0;

    return (bytes) LgColl_activeCount((const Collection*) self);
}

static bytes linkedGraphEdgeCount(const Graph *self)
{
    if(!LinkedGraph_isLinkedGraph(self))
        return 0;

    return Lg_edgeTotal((const LinkedGraph*) self);
}

static const GraphOps *linkedGraphOps()
{
    if(atomic_load_explicit(&s_linkedGraphOpsReady, memory_order_acquire))
        return &s_linkedGraphOps;

    SpinLock_lock(&s_linkedGraphOpsLock);

    if(!atomic_load_explicit(&s_linkedGraphOpsReady, memory_order_relaxed)) {
        GraphOps_setKind(&s_linkedGraphOps, GRAPH_KIND_LINKED);
        GraphOps_setKindName(&s_linkedGraphOps, "LinkedGraph");
        GraphOps_setAddVertexBytes(&s_linkedGraphOps, LinkedGraph_addVertexBytes);
        GraphOps_setEmplaceVertex(&s_linkedGraphOps, LinkedGraph_emplaceVertex);
        GraphOps_setVertex(&s_linkedGraphOps, LinkedGraph_vertex);
        GraphOps_setWriteVertexBytes(&s_linkedGraphOps, LinkedGraph_setVertexBytes);
        GraphOps_setAddEdgeBytes(&s_linkedGraphOps, LinkedGraph_addEdgeBytes);
        GraphOps_setEdge(&s_linkedGraphOps, LinkedGraph_edge);
        GraphOps_setRemoveEdge(&s_linkedGraphOps, LinkedGraph_removeEdge);
        GraphOps_setOutDegree(&s_linkedGraphOps, LinkedGraph_outDegree);
        GraphOps_setForEachEdge(&s_linkedGraphOps, LinkedGraph_forEachEdge);
        GraphOps_setNeighbors(&s_linkedGraphOps, LinkedGraph_neighbors);
        GraphOps_setClear(&s_linkedGraphOps, LinkedGraph_clear);
        GraphOps_setVertexCount(&s_linkedGraphOps, linkedGraphVertexCount);
        GraphOps_setEdgeCount(&s_linkedGraphOps, linkedGraphEdgeCount);
        GraphOps_setDestroy(&s_linkedGraphOps, LinkedGraph_free);
        atomic_store_explicit(&s_linkedGraphOpsReady, true, memory_order_release);
    }

    SpinLock_unlock(&s_linkedGraphOpsLock);

    return &s_linkedGraphOps;
}

;;CONSTRUCTOR
Graph *LinkedGraph_newLayout(ElementLayout vertexLayout, ElementLayout edgeLayout,
                             MemoryArena *arena)
{
    if(!ElementLayout_isValid(vertexLayout) || !ElementLayout_isValid(edgeLayout)) {
        THROW("LinkedGraph: invalid layout for <%s, %s>",
              ElementLayout_typeName(vertexLayout),
              ElementLayout_typeName(edgeLayout));

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    LinkedGraph *lg = (LinkedGraph*) Memory_alloc(TYPE_LINKED_GRAPH, targetArena, sizeof(LinkedGraph));

    if(!lg)
        return nullptr;

    Graph *base = (Graph*) lg;

    if(!Graph_initBase(base, TYPE_LINKED_GRAPH, linkedGraphOps(),
                       vertexLayout, edgeLayout, targetArena)) {
        MemoryArena_free(targetArena, lg);

        return nullptr;
    }

    Lg_setIndex(lg, nullptr);
    Lg_setHead(lg, nullptr);
    Lg_setTail(lg, nullptr);
    Lg_setVertexNodeBytes(lg, sizeof(LinkedGraphVertex) + ElementLayout_stride(vertexLayout));
    Lg_setEdgeNodeBytes(lg, sizeof(LinkedGraphEdge) + ElementLayout_stride(edgeLayout));
    Lg_setEdgeTotal(lg, 0);

    return base;
}

bool LinkedGraph_isLinkedGraph(const Graph *self)
{
    return Graph_kind(self) == GRAPH_KIND_LINKED;
}

any LinkedGraph_emplaceVertex(Graph *self, u32 *outId)
{
    if(outId)
        *outId = GRAPH_INVALID_VERTEX;

    if(!LinkedGraph_isLinkedGraph(self))
        return nullptr;

    LinkedGraph *lg = (LinkedGraph*) self;
    Collection *c = (Collection*) self;
    u32 active = LgColl_activeCount(c);

    if(active >= UINT32_MAX - 1u) {
        (discard) Collection_reportExhaustion(c, 0);

        return nullptr;
    }

    if(active >= LgColl_capacity(c)) {
        if(!linkedGraphGrowIndex(lg, (bytes) active + 1u))
            return nullptr;
    }

    bytes vNodeBytes = Lg_vertexNodeBytes(lg);
    LinkedGraphVertex *v = (LinkedGraphVertex*) Memory_alloc(
        TYPE_LINKED_GRAPH_NODE, Graph_arena(self), vNodeBytes);

    if(!v) {
        (discard) Collection_reportExhaustion(c, vNodeBytes);

        return nullptr;
    }

    LgVert_setNext(v, nullptr);
    LgVert_setEdges(v, nullptr);
    LgVert_setId(v, active);
    LgVert_setOutDegree(v, 0);
    LgVert_setPad(v);

    u8 *payload = lgVertPayload(v);
    memset(payload, 0, Graph_vertexStride(self));

    LinkedGraphVertex *oldTail = Lg_tail(lg);

    if(oldTail)
        LgVert_setNext(oldTail, v);
    else
        Lg_setHead(lg, v);

    Lg_setTail(lg, v);
    Lg_index(lg)[active] = v;
    LgColl_setActiveCount(c, active + 1u);

    if(outId)
        *outId = active;

    return payload;
}

u32 LinkedGraph_addVertexBytes(Graph *self, const_any vertex, bytes vertexBytes)
{
    if(!LinkedGraph_isLinkedGraph(self) || !vertex || vertexBytes != Graph_vertexStride(self))
        return GRAPH_INVALID_VERTEX;

    u32 id = GRAPH_INVALID_VERTEX;
    any slot = LinkedGraph_emplaceVertex(self, &id);

    if(!slot)
        return GRAPH_INVALID_VERTEX;

    memcpy(slot, vertex, vertexBytes);

    return id;
}

;;HOTCODE
any LinkedGraph_vertex(const Graph *self, u32 id)
{
    if(!LinkedGraph_isLinkedGraph(self))
        return nullptr;

    const LinkedGraph *lg = (const LinkedGraph*) self;
    const Collection *c = (const Collection*) self;

    if(id >= LgColl_activeCount(c))
        return nullptr;

    LinkedGraphVertex **idx = Lg_index(lg);

    if(!idx || !idx[id])
        return nullptr;

    return lgVertPayload(idx[id]);
}

bool LinkedGraph_setVertexBytes(Graph *self, u32 id, const_any vertex, bytes vertexBytes)
{
    if(!LinkedGraph_isLinkedGraph(self) || !vertex || vertexBytes != Graph_vertexStride(self))
        return false;

    any slot = LinkedGraph_vertex(self, id);

    if(!slot)
        return false;

    memcpy(slot, vertex, vertexBytes);

    return true;
}

bool LinkedGraph_addEdgeBytes(Graph *self, u32 from, u32 to, const_any edge, bytes edgeBytes)
{
    if(!LinkedGraph_isLinkedGraph(self) || !edge || edgeBytes != Graph_edgeStride(self))
        return false;

    LinkedGraph *lg = (LinkedGraph*) self;
    Collection *c = (Collection*) self;
    u32 active = LgColl_activeCount(c);

    if(from >= active || to >= active)
        return false;

    LinkedGraphVertex *src = Lg_index(lg)[from];
    LinkedGraphEdge *cur = LgVert_edges(src);
    LinkedGraphEdge *last = nullptr;

    while(cur) {
        if(LgEdge_to(cur) == to) {
            memcpy(lgEdgePayload(cur), edge, edgeBytes);

            return true;
        }

        last = cur;
        cur = LgEdge_next(cur);
    }

    bytes eNodeBytes = Lg_edgeNodeBytes(lg);
    LinkedGraphEdge *node = (LinkedGraphEdge*) Memory_alloc(
        TYPE_LINKED_GRAPH_NODE, Graph_arena(self), eNodeBytes);

    if(!node)
        return Collection_reportExhaustion(c, eNodeBytes);

    LgEdge_setNext(node, nullptr);
    LgEdge_setTo(node, to);
    LgEdge_setPads(node);
    memcpy(lgEdgePayload(node), edge, edgeBytes);

    if(last)
        LgEdge_setNext(last, node);
    else
        LgVert_setEdges(src, node);

    LgVert_setOutDegree(src, LgVert_outDegree(src) + 1u);
    Lg_setEdgeTotal(lg, Lg_edgeTotal(lg) + 1u);

    return true;
}

;;HOTCODE
any LinkedGraph_edge(const Graph *self, u32 from, u32 to)
{
    if(!LinkedGraph_isLinkedGraph(self))
        return nullptr;

    const LinkedGraph *lg = (const LinkedGraph*) self;
    const Collection *c = (const Collection*) self;
    u32 active = LgColl_activeCount(c);

    if(from >= active || to >= active)
        return nullptr;

    LinkedGraphVertex *src = Lg_index(lg)[from];
    LinkedGraphEdge *cur = LgVert_edges(src);

    while(cur) {
        if(LgEdge_to(cur) == to)
            return lgEdgePayload(cur);

        cur = LgEdge_next(cur);
    }

    return nullptr;
}

bool LinkedGraph_removeEdge(Graph *self, u32 from, u32 to)
{
    if(!LinkedGraph_isLinkedGraph(self))
        return false;

    LinkedGraph *lg = (LinkedGraph*) self;
    Collection *c = (Collection*) self;
    u32 active = LgColl_activeCount(c);

    if(from >= active || to >= active)
        return false;

    LinkedGraphVertex *src = Lg_index(lg)[from];
    LinkedGraphEdge *cur = LgVert_edges(src);
    LinkedGraphEdge *prev = nullptr;

    while(cur) {
        if(LgEdge_to(cur) == to) {
            LinkedGraphEdge *next = LgEdge_next(cur);

            if(prev)
                LgEdge_setNext(prev, next);
            else
                LgVert_setEdges(src, next);

            if(LgVert_outDegree(src) > 0)
                LgVert_setOutDegree(src, LgVert_outDegree(src) - 1u);

            if(Lg_edgeTotal(lg) > 0)
                Lg_setEdgeTotal(lg, Lg_edgeTotal(lg) - 1u);

            MemoryArena_free(Graph_arena(self), cur);

            return true;
        }

        prev = cur;
        cur = LgEdge_next(cur);
    }

    return false;
}

bytes LinkedGraph_outDegree(const Graph *self, u32 from)
{
    if(!LinkedGraph_isLinkedGraph(self))
        return 0;

    const LinkedGraph *lg = (const LinkedGraph*) self;
    const Collection *c = (const Collection*) self;

    if(from >= LgColl_activeCount(c))
        return 0;

    return (bytes) LgVert_outDegree(Lg_index(lg)[from]);
}

fn LinkedGraph_forEachEdge(Graph *self, u32 from, GraphEdgeVisitFn visit, any userdata)
{
    if(!LinkedGraph_isLinkedGraph(self) || !visit)
        return;

    LinkedGraph *lg = (LinkedGraph*) self;
    Collection *c = (Collection*) self;

    if(from >= LgColl_activeCount(c))
        return;

    LinkedGraphVertex *src = Lg_index(lg)[from];
    LinkedGraphEdge *cur = LgVert_edges(src);
    bytes idx = 0;

    while(cur) {
        LinkedGraphEdge *next = LgEdge_next(cur);
        visit(from, LgEdge_to(cur), lgEdgePayload(cur), idx, userdata);
        idx++;
        cur = next;
    }
}

List *LinkedGraph_neighbors(const Graph *self, u32 from)
{
    if(!LinkedGraph_isLinkedGraph(self))
        return nullptr;

    const LinkedGraph *lg = (const LinkedGraph*) self;
    const Collection *c = (const Collection*) self;

    if(from >= LgColl_activeCount(c))
        return nullptr;

    LinkedGraphVertex *src = Lg_index(lg)[from];
    u32 deg = LgVert_outDegree(src);
    List *out = ArrayList_newLayout(ElementLayout_of(u32), deg < 4u ? 4u : deg, Graph_arena(self));

    if(!out)
        return nullptr;

    LinkedGraphEdge *cur = LgVert_edges(src);

    while(cur) {
        u32 to = LgEdge_to(cur);

        if(!List_addBytes(out, &to, sizeof(u32))) {
            List_free(out);

            return nullptr;
        }

        cur = LgEdge_next(cur);
    }

    return out;
}

fn LinkedGraph_clear(Graph *self)
{
    if(!LinkedGraph_isLinkedGraph(self))
        return;

    LinkedGraph *lg = (LinkedGraph*) self;
    Collection *c = (Collection*) self;
    MemoryArena *arena = Graph_arena(self);
    LinkedGraphVertex *v = Lg_head(lg);

    while(v) {
        LinkedGraphVertex *nextV = LgVert_next(v);
        LinkedGraphEdge *e = LgVert_edges(v);

        while(e) {
            LinkedGraphEdge *nextE = LgEdge_next(e);
            MemoryArena_free(arena, e);
            e = nextE;
        }

        MemoryArena_free(arena, v);
        v = nextV;
    }

    Lg_setHead(lg, nullptr);
    Lg_setTail(lg, nullptr);
    LgColl_setActiveCount(c, 0);
    Lg_setEdgeTotal(lg, 0);
}

;;DECONSTRUCT
fn LinkedGraph_free(Graph *self)
{
    if(!LinkedGraph_isLinkedGraph(self))
        return;

    LinkedGraph *lg = (LinkedGraph*) self;
    MemoryArena *arena = Graph_arena(self);
    LinkedGraph_clear(self);

    LinkedGraphVertex **idx = Lg_index(lg);

    if(idx) {
        MemoryArena_free(arena, idx);
        Lg_setIndex(lg, nullptr);
    }

    MemoryArena_free(arena, self);
}

#include "vex.h"
#include "nio/mem.h"
#include "struct/graph.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Graph
 * ============================================================================
 * Polymorphic directed/undirected Graph interface owned by Relational Engine
 * (lifetime tier). Concrete implementations (ArrayGraph, LinkedGraph) embed
 * Graph at offset zero, register a GraphOps vtable, and store arbitrary inline
 * vertex payloads (V) and edge payloads (E, defaulting to f32 weight) inside
 * the owning MemoryArena.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Graph (struct/graph.c)
 * ============================================================================
 * Polymorphic Graph interface over Collection + GraphOps + V/E layouts.
 *
 * --CONSTANTS----------------------------
 * public   const u32 GRAPH_INVALID_VERTEX      sentinel vertex id (UINT32_MAX)
 *
 * --FIELDS-------------------------------
 * public   Collection      collection          embedded Collection header, 1:1
 * public   const GraphOps *ops                 borrows GraphOps vtable, 1:1
 * public   MemoryArena    *arena               borrows MemoryArena, 1:1
 * public   ElementLayout   vertexLayout        vertex type layout
 * public   ElementLayout   edgeLayout          edge type layout
 *
 * --CONSTRUCTORS-------------------------
 * public   Graph_initBase(...)                 initializes embedded Graph base
 *
 * --FUNCTIONS----------------------------
 * public   Graph_addVertexBytes / emplaceVertex / vertex / vertexCopyOut / setVertexBytes
 * public   Graph_addEdgeBytes / addBiEdgeBytes / edge / edgeCopyOut / hasEdge / removeEdge
 * public   Graph_outDegree / forEachEdge / neighbors / clear / free
 * public   Graph_toString / toStringStruct     cold projections
 * private  formatWrite(...)                    bounded snprintf helper
 *
 * --GETTERS------------------------------
 * public   Graph_vertexCount / edgeCount / isEmpty
 * public   Graph_vertexStride / edgeStride / vertexTypeName / edgeTypeName
 * public   Graph_kind / kindName / arena / ops / vertexLayout / edgeLayout / exhaustionCount
 * private  GraphOps_*                          vtable field readers
 *
 * --SETTERS------------------------------
 * public   GraphOps_set*(...)                  vtable population setters
 * private  Graph_set* / GraphColl_set*         internal field writers
 * ============================================================================
 */

;;GETTER
static inline GraphKind GraphOps_kind(const GraphOps *ops)
{
    if(!ops)
        return GRAPH_KIND_UNKNOWN;

    return (*ops).kind;
}

;;GETTER
static inline string GraphOps_kindName(const GraphOps *ops)
{
    if(!ops || !(*ops).kindName)
        return "UnknownGraph";

    return (*ops).kindName;
}

;;GETTER
static inline GraphAddVertexBytesFn GraphOps_addVertexBytes(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).addVertexBytes;
}

;;GETTER
static inline GraphEmplaceVertexFn GraphOps_emplaceVertex(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).emplaceVertex;
}

;;GETTER
static inline GraphVertexFn GraphOps_vertex(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).vertex;
}

;;GETTER
static inline GraphWriteVertexBytesFn GraphOps_writeVertexBytes(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).writeVertexBytes;
}

;;GETTER
static inline GraphAddEdgeBytesFn GraphOps_addEdgeBytes(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).addEdgeBytes;
}

;;GETTER
static inline GraphEdgeFn GraphOps_edge(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).edge;
}

;;GETTER
static inline GraphRemoveEdgeFn GraphOps_removeEdge(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).removeEdge;
}

;;GETTER
static inline GraphOutDegreeFn GraphOps_outDegree(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).outDegree;
}

;;GETTER
static inline GraphForEachEdgeFn GraphOps_forEachEdge(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).forEachEdge;
}

;;GETTER
static inline GraphNeighborsFn GraphOps_neighbors(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).neighbors;
}

;;GETTER
static inline GraphClearFn GraphOps_clear(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).clear;
}

;;GETTER
static inline GraphVertexCountFn GraphOps_vertexCount(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).vertexCount;
}

;;GETTER
static inline GraphEdgeCountFn GraphOps_edgeCount(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).edgeCount;
}

;;GETTER
static inline GraphDestroyFn GraphOps_destroy(const GraphOps *ops)
{
    if(!ops)
        return nullptr;

    return (*ops).destroy;
}

;;SETTER
fn GraphOps_setKind(GraphOps *ops, GraphKind kind)
{
    if(!ops)
        return;

    (*ops).kind = kind;
}

;;SETTER
fn GraphOps_setKindName(GraphOps *ops, string kindName)
{
    if(!ops)
        return;

    (*ops).kindName = kindName;
}

;;SETTER
fn GraphOps_setAddVertexBytes(GraphOps *ops, GraphAddVertexBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).addVertexBytes = fnPtr;
}

;;SETTER
fn GraphOps_setEmplaceVertex(GraphOps *ops, GraphEmplaceVertexFn fnPtr)
{
    if(!ops)
        return;

    (*ops).emplaceVertex = fnPtr;
}

;;SETTER
fn GraphOps_setVertex(GraphOps *ops, GraphVertexFn fnPtr)
{
    if(!ops)
        return;

    (*ops).vertex = fnPtr;
}

;;SETTER
fn GraphOps_setWriteVertexBytes(GraphOps *ops, GraphWriteVertexBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).writeVertexBytes = fnPtr;
}

;;SETTER
fn GraphOps_setAddEdgeBytes(GraphOps *ops, GraphAddEdgeBytesFn fnPtr)
{
    if(!ops)
        return;

    (*ops).addEdgeBytes = fnPtr;
}

;;SETTER
fn GraphOps_setEdge(GraphOps *ops, GraphEdgeFn fnPtr)
{
    if(!ops)
        return;

    (*ops).edge = fnPtr;
}

;;SETTER
fn GraphOps_setRemoveEdge(GraphOps *ops, GraphRemoveEdgeFn fnPtr)
{
    if(!ops)
        return;

    (*ops).removeEdge = fnPtr;
}

;;SETTER
fn GraphOps_setOutDegree(GraphOps *ops, GraphOutDegreeFn fnPtr)
{
    if(!ops)
        return;

    (*ops).outDegree = fnPtr;
}

;;SETTER
fn GraphOps_setForEachEdge(GraphOps *ops, GraphForEachEdgeFn fnPtr)
{
    if(!ops)
        return;

    (*ops).forEachEdge = fnPtr;
}

;;SETTER
fn GraphOps_setNeighbors(GraphOps *ops, GraphNeighborsFn fnPtr)
{
    if(!ops)
        return;

    (*ops).neighbors = fnPtr;
}

;;SETTER
fn GraphOps_setClear(GraphOps *ops, GraphClearFn fnPtr)
{
    if(!ops)
        return;

    (*ops).clear = fnPtr;
}

;;SETTER
fn GraphOps_setVertexCount(GraphOps *ops, GraphVertexCountFn fnPtr)
{
    if(!ops)
        return;

    (*ops).vertexCount = fnPtr;
}

;;SETTER
fn GraphOps_setEdgeCount(GraphOps *ops, GraphEdgeCountFn fnPtr)
{
    if(!ops)
        return;

    (*ops).edgeCount = fnPtr;
}

;;SETTER
fn GraphOps_setDestroy(GraphOps *ops, GraphDestroyFn fnPtr)
{
    if(!ops)
        return;

    (*ops).destroy = fnPtr;
}

;;SETTER
static inline fn Graph_setOps(Graph *self, const GraphOps *ops)
{
    (*self).ops = ops;
}

;;SETTER
static inline fn Graph_setArena(Graph *self, MemoryArena *arena)
{
    (*self).arena = arena;
}

;;SETTER
static inline fn Graph_setVertexLayout(Graph *self, ElementLayout layout)
{
    (*self).vertexLayout = layout;
}

;;SETTER
static inline fn Graph_setEdgeLayout(Graph *self, ElementLayout layout)
{
    (*self).edgeLayout = layout;
}

;;SETTER
static inline fn GraphColl_setTypeId(Collection *c, u64 typeId)
{
    (*c).typeId = typeId;
}

;;SETTER
static inline fn GraphColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn GraphColl_setElementClass(Collection *c, u32 cls)
{
    (*c).elementClass = cls;
}

;;SETTER
static inline fn GraphColl_setStride(Collection *c, u32 stride)
{
    (*c).stride = stride;
}

;;SETTER
static inline fn GraphColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;SETTER
static inline fn GraphColl_setHead(Collection *c, u32 head)
{
    (*c).head = head;
}

;;SETTER
static inline fn GraphColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

;;SETTER
static inline fn GraphColl_setExhaustionCount(Collection *c, u64 count)
{
    (*c).exhaustionCount = count;
}

;;SETTER
static inline fn GraphColl_setExhaustionReported(Collection *c, bool rep)
{
    (*c).exhaustionReported = rep;
}

;;GETTER
const GraphOps *Graph_ops(const Graph *self)
{
    if(!self)
        return nullptr;

    return (*self).ops;
}

;;GETTER
MemoryArena *Graph_arena(const Graph *self)
{
    if(!self)
        return nullptr;

    return (*self).arena;
}

;;GETTER
ElementLayout Graph_vertexLayout(const Graph *self)
{
    if(!self)
        return ElementLayout_make(0, 0, nullptr);

    return (*self).vertexLayout;
}

;;GETTER
ElementLayout Graph_edgeLayout(const Graph *self)
{
    if(!self)
        return ElementLayout_make(0, 0, nullptr);

    return (*self).edgeLayout;
}

static fn formatWrite(utf8 *dest, bytes cap, bool *outTruncated, i32 needed)
{
    if(outTruncated)
        *outTruncated = needed < 0 || (bytes) needed >= cap;

    if(cap > 0 && needed < 0)
        dest[0] = '\0';
}

;;CONSTRUCTOR
bool Graph_initBase(Graph *self, u64 typeId, const GraphOps *ops,
                    ElementLayout vertexLayout, ElementLayout edgeLayout, MemoryArena *arena)
{
    if(!self || !ops)
        return false;

    if(!ElementLayout_isValid(vertexLayout) || !ElementLayout_isValid(edgeLayout)) {
        THROW("Graph_initBase: invalid layout for <%s, %s>",
              ElementLayout_typeName(vertexLayout),
              ElementLayout_typeName(edgeLayout));

        return false;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return false;

    Collection *c = (Collection*) self;
    GraphColl_setTypeId(c, typeId);
    GraphColl_setActiveCount(c, 0);
    GraphColl_setElementClass(c, 0);
    GraphColl_setStride(c, (u32) ElementLayout_stride(vertexLayout));
    GraphColl_setCapacity(c, 0);
    GraphColl_setHead(c, 0);
    GraphColl_setData(c, nullptr);
    GraphColl_setExhaustionCount(c, 0);
    GraphColl_setExhaustionReported(c, false);

    Graph_setOps(self, ops);
    Graph_setArena(self, targetArena);
    Graph_setVertexLayout(self, vertexLayout);
    Graph_setEdgeLayout(self, edgeLayout);

    return true;
}

u32 Graph_addVertexBytes(Graph *self, const_any vertex, bytes vertexBytes)
{
    if(!self || !vertex)
        return GRAPH_INVALID_VERTEX;

    if(vertexBytes != Graph_vertexStride(self)) {
        THROW("Graph_addVertex: width %zu does not match vertex '%s' stride %zu",
              vertexBytes, Graph_vertexTypeName(self), Graph_vertexStride(self));

        return GRAPH_INVALID_VERTEX;
    }

    GraphAddVertexBytesFn addFn = GraphOps_addVertexBytes(Graph_ops(self));

    if(!addFn)
        return GRAPH_INVALID_VERTEX;

    return addFn(self, vertex, vertexBytes);
}

any Graph_emplaceVertex(Graph *self, u32 *outId)
{
    if(outId)
        *outId = GRAPH_INVALID_VERTEX;

    if(!self)
        return nullptr;

    GraphEmplaceVertexFn empFn = GraphOps_emplaceVertex(Graph_ops(self));

    if(!empFn)
        return nullptr;

    return empFn(self, outId);
}

;;HOTCODE
any Graph_vertex(const Graph *self, u32 id)
{
    if(!self)
        return nullptr;

    GraphVertexFn vFn = GraphOps_vertex(Graph_ops(self));

    if(!vFn)
        return nullptr;

    return vFn(self, id);
}

bool Graph_vertexCopyOut(const Graph *self, u32 id, any dest, bytes vertexBytes, string typeName)
{
    if(!self || !dest)
        return false;

    if(vertexBytes != Graph_vertexStride(self)) {
        THROW("Graph_vertexAs: width %zu for '%s' does not match vertex '%s' stride %zu",
              vertexBytes, typeName ? typeName : "unknown",
              Graph_vertexTypeName(self), Graph_vertexStride(self));

        return false;
    }

    any slot = Graph_vertex(self, id);

    if(!slot)
        return false;

    memcpy(dest, slot, vertexBytes);

    return true;
}

bool Graph_setVertexBytes(Graph *self, u32 id, const_any vertex, bytes vertexBytes)
{
    if(!self || !vertex)
        return false;

    if(vertexBytes != Graph_vertexStride(self)) {
        THROW("Graph_setVertex: width %zu does not match vertex '%s' stride %zu",
              vertexBytes, Graph_vertexTypeName(self), Graph_vertexStride(self));

        return false;
    }

    GraphWriteVertexBytesFn writeFn = GraphOps_writeVertexBytes(Graph_ops(self));

    if(!writeFn)
        return false;

    return writeFn(self, id, vertex, vertexBytes);
}

bool Graph_addEdgeBytes(Graph *self, u32 from, u32 to, const_any edge, bytes edgeBytes)
{
    if(!self || !edge)
        return false;

    if(edgeBytes != Graph_edgeStride(self)) {
        THROW("Graph_addEdge: width %zu does not match edge '%s' stride %zu",
              edgeBytes, Graph_edgeTypeName(self), Graph_edgeStride(self));

        return false;
    }

    GraphAddEdgeBytesFn addFn = GraphOps_addEdgeBytes(Graph_ops(self));

    if(!addFn)
        return false;

    return addFn(self, from, to, edge, edgeBytes);
}

bool Graph_addBiEdgeBytes(Graph *self, u32 a, u32 b, const_any edge, bytes edgeBytes)
{
    if(!self || !edge)
        return false;

    bool hadAb = Graph_hasEdge(self, a, b);

    if(!Graph_addEdgeBytes(self, a, b, edge, edgeBytes))
        return false;

    if(a == b)
        return true;

    if(!Graph_addEdgeBytes(self, b, a, edge, edgeBytes)) {
        if(!hadAb)
            (discard) Graph_removeEdge(self, a, b);

        return false;
    }

    return true;
}

;;HOTCODE
any Graph_edge(const Graph *self, u32 from, u32 to)
{
    if(!self)
        return nullptr;

    GraphEdgeFn eFn = GraphOps_edge(Graph_ops(self));

    if(!eFn)
        return nullptr;

    return eFn(self, from, to);
}

bool Graph_edgeCopyOut(const Graph *self, u32 from, u32 to, any dest, bytes edgeBytes, string typeName)
{
    if(!self || !dest)
        return false;

    if(edgeBytes != Graph_edgeStride(self)) {
        THROW("Graph_edgeAs: width %zu for '%s' does not match edge '%s' stride %zu",
              edgeBytes, typeName ? typeName : "unknown",
              Graph_edgeTypeName(self), Graph_edgeStride(self));

        return false;
    }

    any slot = Graph_edge(self, from, to);

    if(!slot)
        return false;

    memcpy(dest, slot, edgeBytes);

    return true;
}

bool Graph_hasEdge(const Graph *self, u32 from, u32 to)
{
    return Graph_edge(self, from, to) != nullptr;
}

bool Graph_removeEdge(Graph *self, u32 from, u32 to)
{
    if(!self)
        return false;

    GraphRemoveEdgeFn remFn = GraphOps_removeEdge(Graph_ops(self));

    if(!remFn)
        return false;

    return remFn(self, from, to);
}

bytes Graph_outDegree(const Graph *self, u32 from)
{
    if(!self)
        return 0;

    GraphOutDegreeFn degFn = GraphOps_outDegree(Graph_ops(self));

    if(!degFn)
        return 0;

    return degFn(self, from);
}

fn Graph_forEachEdge(Graph *self, u32 from, GraphEdgeVisitFn visit, any userdata)
{
    if(!self || !visit)
        return;

    GraphForEachEdgeFn eachFn = GraphOps_forEachEdge(Graph_ops(self));

    if(eachFn)
        eachFn(self, from, visit, userdata);
}

List *Graph_neighbors(const Graph *self, u32 from)
{
    if(!self)
        return nullptr;

    GraphNeighborsFn nFn = GraphOps_neighbors(Graph_ops(self));

    if(!nFn)
        return nullptr;

    return nFn(self, from);
}

fn Graph_clear(Graph *self)
{
    if(!self)
        return;

    GraphClearFn clearFn = GraphOps_clear(Graph_ops(self));

    if(clearFn)
        clearFn(self);
}

;;DECONSTRUCT
fn Graph_free(Graph *self)
{
    if(!self)
        return;

    GraphDestroyFn destroyFn = GraphOps_destroy(Graph_ops(self));

    if(destroyFn)
        destroyFn(self);
}

bytes Graph_vertexCount(const Graph *self)
{
    if(!self)
        return 0;

    GraphVertexCountFn vcFn = GraphOps_vertexCount(Graph_ops(self));

    if(!vcFn)
        return 0;

    return vcFn(self);
}

bytes Graph_edgeCount(const Graph *self)
{
    if(!self)
        return 0;

    GraphEdgeCountFn ecFn = GraphOps_edgeCount(Graph_ops(self));

    if(!ecFn)
        return 0;

    return ecFn(self);
}

bool Graph_isEmpty(const Graph *self)
{
    return Graph_vertexCount(self) == 0;
}

bytes Graph_vertexStride(const Graph *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(Graph_vertexLayout(self));
}

bytes Graph_edgeStride(const Graph *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(Graph_edgeLayout(self));
}

string Graph_vertexTypeName(const Graph *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(Graph_vertexLayout(self));
}

string Graph_edgeTypeName(const Graph *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(Graph_edgeLayout(self));
}

GraphKind Graph_kind(const Graph *self)
{
    if(!self)
        return GRAPH_KIND_UNKNOWN;

    return GraphOps_kind(Graph_ops(self));
}

string Graph_kindName(const Graph *self)
{
    if(!self)
        return "nullptr";

    return GraphOps_kindName(Graph_ops(self));
}

;;TEST
u64 Graph_exhaustionCount(const Graph *self)
{
    if(!self)
        return 0;

    return Collection_exhaustionCount((const Collection*) self);
}

fn Graph_toString(const Graph *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 needed = snprintf(dest, cap, "Graph(nullptr)");
        formatWrite(dest, cap, outTruncated, needed);

        return;
    }

    i32 needed = snprintf(dest, cap, "%s<%s, %s>(vertices=%zu, edges=%zu)",
                          Graph_kindName(self),
                          Graph_vertexTypeName(self),
                          Graph_edgeTypeName(self),
                          Graph_vertexCount(self),
                          Graph_edgeCount(self));
    formatWrite(dest, cap, outTruncated, needed);
}

fn Graph_toStringStruct(const Graph *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 needed = snprintf(dest, cap, "Graph{nullptr}");
        formatWrite(dest, cap, outTruncated, needed);

        return;
    }

    i32 needed = snprintf(dest, cap,
                          "Graph{kind=%s, vertex=%s(%zu), edge=%s(%zu), vertices=%zu, edges=%zu, exhausted=%llu}",
                          Graph_kindName(self),
                          Graph_vertexTypeName(self),
                          Graph_vertexStride(self),
                          Graph_edgeTypeName(self),
                          Graph_edgeStride(self),
                          Graph_vertexCount(self),
                          Graph_edgeCount(self),
                          (u64) Graph_exhaustionCount(self));
    formatWrite(dest, cap, outTruncated, needed);
}

#include "vex.h"
#include <stdckdint.h>

#include "nio/mem.h"
#include "struct/exhaustion.h"
#include "struct/stack.h"
#include "type/re_type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Stack
 * ============================================================================
 * Polymorphic LIFO Stack owned by Relational Engine (lifetime tier) providing
 * two arena-backed implementations behind one handle: ArrayStack (default
 * contiguous flat buffer with geometric growth) and LinkedStack (32-byte-
 * aligned singly-linked nodes with pointer stability). Ships both bool
 * Stack_push and TryValue Stack_pushTry observability paths.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Stack (struct/stack.c)
 * ============================================================================
 * LIFO stack supporting ArrayStack (contiguous) and LinkedStack (node chain).
 *
 * --CONSTANTS----------------------------
 * public   const u32 ARRAY_STACK_DEFAULT_CAPACITY  default array slots (16)
 *
 * --FIELDS-------------------------------
 * public   Collection    collection                embedded Collection, 1:1
 * public   StackKind     kind                      concrete backing tag
 * public   MemoryArena  *arena                     borrows MemoryArena, 1:1
 * public   ElementLayout layout                    element layout metadata
 * private  StackNode    *topNode                   owns linked stack top, 1:N
 *
 * --CONSTRUCTORS-------------------------
 * public   ArrayStack_newLayout(...)               contiguous array stack
 * public   LinkedStack_newLayout(...)              linked node stack
 *
 * --FUNCTIONS----------------------------
 * public   ArrayStack_isArrayStack(...)            checks STACK_KIND_ARRAY
 * public   LinkedStack_isLinkedStack(...)          checks STACK_KIND_LINKED
 * public   Stack_pushBytes(...)                    stride-checked LIFO push
 * public   Stack_pushBytesTry(...)                 observable TryValue push
 * public   Stack_emplace(...)                      pushes zeroed top slot
 * public   Stack_peek / peekCopyOut                top element lookup
 * public   Stack_pop / popCopyOut                  removes top element
 * public   Stack_get / copyOut                     bottom-up indexed read
 * public   Stack_clear / reserve / free            lifecycle operations
 * public   Stack_toString / toStringStruct         cold projections
 * private  arrayStackGrow(...)                     checked geometric growth
 *
 * --GETTERS------------------------------
 * public   Stack_count / size / length / capacity / isEmpty / stride / typeName
 * public   Stack_kind / kindName / arena / layout / exhaustionCount
 * private  StNode_below / St_topNode / StColl_*
 *
 * --SETTERS------------------------------
 * private  StNode_set* / St_set* / StColl_set*
 * ============================================================================
 */

;;GETTER
static inline StackNode *StNode_below(const StackNode *n)
{
    return (*n).below;
}

;;SETTER
static inline fn StNode_setBelow(StackNode *n, StackNode *below)
{
    (*n).below = below;
}

;;SETTER
static inline fn StNode_setPads(StackNode *n)
{
    (*n).pad0 = 0;
    (*n).pad1 = 0;
    (*n).pad2 = 0;
}

;;GETTER
static inline StackNode *St_topNode(const Stack *s)
{
    return (*s).topNode;
}

;;GETTER
StackKind Stack_kind(const Stack *self)
{
    if(!self)
        return STACK_KIND_UNKNOWN;

    return (*self).kind;
}

;;GETTER
MemoryArena *Stack_arena(const Stack *self)
{
    if(!self)
        return nullptr;

    return (*self).arena;
}

;;GETTER
ElementLayout Stack_layout(const Stack *self)
{
    if(!self)
        return ElementLayout_make(0, 0, nullptr);

    return (*self).layout;
}

;;SETTER
static inline fn St_setKind(Stack *s, StackKind kind)
{
    (*s).kind = kind;
}

;;SETTER
static inline fn St_setArena(Stack *s, MemoryArena *arena)
{
    (*s).arena = arena;
}

;;SETTER
static inline fn St_setLayout(Stack *s, ElementLayout layout)
{
    (*s).layout = layout;
}

;;SETTER
static inline fn St_setTopNode(Stack *s, StackNode *node)
{
    (*s).topNode = node;
}

;;GETTER
static inline u32 StColl_activeCount(const Collection *c)
{
    return (*c).activeCount;
}

;;GETTER
static inline u32 StColl_capacity(const Collection *c)
{
    return (*c).capacity;
}

;;GETTER
static inline u8 *StColl_data(const Collection *c)
{
    return (*c).data;
}

;;SETTER
static inline fn StColl_setTypeId(Collection *c, u64 typeId)
{
    (*c).typeId = typeId;
}

;;SETTER
static inline fn StColl_setActiveCount(Collection *c, u32 count)
{
    (*c).activeCount = count;
}

;;SETTER
static inline fn StColl_setElementClass(Collection *c, u32 cls)
{
    (*c).elementClass = cls;
}

;;SETTER
static inline fn StColl_setStride(Collection *c, u32 stride)
{
    (*c).stride = stride;
}

;;SETTER
static inline fn StColl_setCapacity(Collection *c, u32 cap)
{
    (*c).capacity = cap;
}

;;SETTER
static inline fn StColl_setHead(Collection *c, u32 head)
{
    (*c).head = head;
}

;;SETTER
static inline fn StColl_setData(Collection *c, u8 *data)
{
    (*c).data = data;
}

;;SETTER
static inline fn StColl_setExhaustionCount(Collection *c, u64 count)
{
    (*c).exhaustionCount = count;
}

;;SETTER
static inline fn StColl_setExhaustionReported(Collection *c, bool rep)
{
    (*c).exhaustionReported = rep;
}

static inline u8 *stNodeElem(StackNode *node)
{
    return ((u8*) node) + sizeof(StackNode);
}

static fn formatWrite(utf8 *dest, bytes cap, bool *outTruncated, i32 needed)
{
    if(outTruncated)
        *outTruncated = needed < 0 || (bytes) needed >= cap;

    if(cap > 0 && needed < 0)
        dest[0] = '\0';
}

static fn initStackCollection(Collection *c, u64 typeId, bytes stride)
{
    StColl_setTypeId(c, typeId);
    StColl_setActiveCount(c, 0);
    StColl_setElementClass(c, 0);
    StColl_setStride(c, (u32) stride);
    StColl_setCapacity(c, 0);
    StColl_setHead(c, 0);
    StColl_setData(c, nullptr);
    StColl_setExhaustionCount(c, 0);
    StColl_setExhaustionReported(c, false);
}

static TryCode arrayStackGrow(Stack *self, bytes minCapacity)
{
    Collection *c = (Collection*) self;
    u32 oldCap = StColl_capacity(c);
    bytes newCap = oldCap == 0 ? ARRAY_STACK_DEFAULT_CAPACITY : (bytes) oldCap;

    while(newCap < minCapacity) {
        if(newCap > ((bytes) UINT32_MAX >> 1u)) {
            if(minCapacity <= (bytes) UINT32_MAX) {
                newCap = minCapacity;
                break;
            }

            (discard) Collection_reportExhaustion(c, 0);

            return TRY_OVERFLOW;
        }

        newCap <<= 1u;
    }

    if(newCap > (bytes) UINT32_MAX) {
        (discard) Collection_reportExhaustion(c, 0);

        return TRY_OVERFLOW;
    }

    bytes stride = Stack_stride(self);
    bytes allocBytes = 0;

    if(ckd_mul(&allocBytes, newCap, stride)) {
        (discard) Collection_reportExhaustion(c, 0);

        return TRY_OVERFLOW;
    }

    MemoryArena *arena = Stack_arena(self);
    u8 *newBuf = (u8*) Memory_alloc(TYPE_BYTE_ARRAY, arena, allocBytes);

    if(!newBuf) {
        (discard) Collection_reportExhaustion(c, allocBytes);

        return TRY_NO_MEMORY;
    }

    u8 *oldBuf = StColl_data(c);
    u32 active = StColl_activeCount(c);

    if(oldBuf && active > 0)
        memcpy(newBuf, oldBuf, (bytes) active * stride);

    if(oldBuf)
        MemoryArena_free(arena, oldBuf);

    StColl_setData(c, newBuf);
    StColl_setCapacity(c, (u32) newCap);

    return TRY_OK;
}

;;CONSTRUCTOR
Stack *ArrayStack_newLayout(ElementLayout layout, bytes initialCapacity, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("ArrayStack: invalid layout for '%s'", ElementLayout_typeName(layout));

        return nullptr;
    }

    if(initialCapacity > (bytes) UINT32_MAX) {
        THROW("ArrayStack: initial capacity %zu overflows u32", initialCapacity);

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    bytes stride = ElementLayout_stride(layout);
    bytes cap = initialCapacity;
    bytes allocBytes = 0;

    if(cap > 0 && ckd_mul(&allocBytes, cap, stride)) {
        THROW("ArrayStack: initial allocation overflow");

        return nullptr;
    }

    Stack *s = (Stack*) Memory_alloc(TYPE_ARRAY_STACK, targetArena, sizeof(Stack));

    if(!s)
        return nullptr;

    Collection *c = (Collection*) s;
    initStackCollection(c, TYPE_ARRAY_STACK, stride);
    St_setKind(s, STACK_KIND_ARRAY);
    St_setArena(s, targetArena);
    St_setLayout(s, layout);
    St_setTopNode(s, nullptr);

    if(cap > 0) {
        u8 *buf = (u8*) Memory_alloc(TYPE_BYTE_ARRAY, targetArena, allocBytes);

        if(!buf) {
            MemoryArena_free(targetArena, s);

            return nullptr;
        }

        StColl_setData(c, buf);
        StColl_setCapacity(c, (u32) cap);
    }

    return s;
}

;;CONSTRUCTOR
Stack *LinkedStack_newLayout(ElementLayout layout, MemoryArena *arena)
{
    if(!ElementLayout_isValid(layout)) {
        THROW("LinkedStack: invalid layout for '%s'", ElementLayout_typeName(layout));

        return nullptr;
    }

    MemoryArena *targetArena = arena ? arena : Re_arena();

    if(!targetArena)
        return nullptr;

    Stack *s = (Stack*) Memory_alloc(TYPE_LINKED_STACK, targetArena, sizeof(Stack));

    if(!s)
        return nullptr;

    Collection *c = (Collection*) s;
    initStackCollection(c, TYPE_LINKED_STACK, ElementLayout_stride(layout));
    St_setKind(s, STACK_KIND_LINKED);
    St_setArena(s, targetArena);
    St_setLayout(s, layout);
    St_setTopNode(s, nullptr);

    return s;
}

bool ArrayStack_isArrayStack(const Stack *self)
{
    return Stack_kind(self) == STACK_KIND_ARRAY;
}

bool LinkedStack_isLinkedStack(const Stack *self)
{
    return Stack_kind(self) == STACK_KIND_LINKED;
}

static any stackEmplaceCode(Stack *self, TryCode *outCode)
{
    if(outCode)
        *outCode = TRY_NULL_ARG;

    if(!self)
        return nullptr;

    Collection *c = (Collection*) self;
    bytes stride = Stack_stride(self);
    u32 active = StColl_activeCount(c);

    if(active == UINT32_MAX) {
        (discard) Collection_reportExhaustion(c, 0);

        if(outCode)
            *outCode = TRY_OVERFLOW;

        return nullptr;
    }

    if(Stack_kind(self) == STACK_KIND_ARRAY) {
        u32 cap = StColl_capacity(c);

        if(active >= cap) {
            TryCode growCode = arrayStackGrow(self, (bytes) active + 1u);

            if(growCode != TRY_OK) {
                if(outCode)
                    *outCode = growCode;

                return nullptr;
            }
        }

        u8 *slot = StColl_data(c) + ((bytes) active * stride);
        memset(slot, 0, stride);
        StColl_setActiveCount(c, active + 1u);

        if(outCode)
            *outCode = TRY_OK;

        return slot;
    }

    if(Stack_kind(self) == STACK_KIND_LINKED) {
        bytes nodeBytes = sizeof(StackNode) + stride;
        StackNode *node = (StackNode*) Memory_alloc(TYPE_LINKED_NODE, Stack_arena(self), nodeBytes);

        if(!node) {
            (discard) Collection_reportExhaustion(c, nodeBytes);

            if(outCode)
                *outCode = TRY_NO_MEMORY;

            return nullptr;
        }

        StNode_setBelow(node, St_topNode(self));
        StNode_setPads(node);
        u8 *elem = stNodeElem(node);
        memset(elem, 0, stride);
        St_setTopNode(self, node);
        StColl_setActiveCount(c, active + 1u);

        if(outCode)
            *outCode = TRY_OK;

        return elem;
    }

    if(outCode)
        *outCode = TRY_UNSUPPORTED;

    return nullptr;
}

any Stack_emplace(Stack *self)
{
    TryCode code = TRY_OK;

    return stackEmplaceCode(self, &code);
}

TryValue Stack_pushBytesTry(Stack *self, const_any elem, bytes elemBytes)
{
    if(!self || !elem)
        return TryValue_error(TRY_NULL_ARG);

    if(elemBytes != Stack_stride(self)) {
        THROW("Stack_pushTry: width %zu does not match '%s' stride %zu",
              elemBytes, Stack_typeName(self), Stack_stride(self));

        return TryValue_error(TRY_FORMAT);
    }

    TryCode code = TRY_OK;
    any slot = stackEmplaceCode(self, &code);

    if(!slot)
        return TryValue_error(code);

    memcpy(slot, elem, elemBytes);

    u64 valuePreview = 0;
    bytes copyLen = elemBytes < sizeof(u64) ? elemBytes : sizeof(u64);
    memcpy(&valuePreview, elem, copyLen);

    return TryValue_ok(valuePreview);
}

bool Stack_pushBytes(Stack *self, const_any elem, bytes elemBytes)
{
    TryValue res = Stack_pushBytesTry(self, elem, elemBytes);

    return TryValue_isOk(&res);
}

;;HOTCODE
any Stack_peek(const Stack *self)
{
    if(!self)
        return nullptr;

    const Collection *c = (const Collection*) self;
    u32 active = StColl_activeCount(c);

    if(active == 0)
        return nullptr;

    if(Stack_kind(self) == STACK_KIND_ARRAY) {
        u8 *buf = StColl_data(c);

        if(!buf)
            return nullptr;

        return buf + (((bytes) active - 1u) * Stack_stride(self));
    }

    if(Stack_kind(self) == STACK_KIND_LINKED) {
        StackNode *top = St_topNode(self);

        if(!top)
            return nullptr;

        return stNodeElem(top);
    }

    return nullptr;
}

bool Stack_peekCopyOut(const Stack *self, any dest, bytes elemBytes, string typeName)
{
    if(!self || !dest)
        return false;

    if(elemBytes != Stack_stride(self)) {
        THROW("Stack_peekAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Stack_typeName(self), Stack_stride(self));

        return false;
    }

    any top = Stack_peek(self);

    if(!top)
        return false;

    memcpy(dest, top, elemBytes);

    return true;
}

bool Stack_popCopyOut(Stack *self, any dest, bytes elemBytes, string typeName)
{
    if(!self)
        return false;

    if(dest && elemBytes != Stack_stride(self)) {
        THROW("Stack_popAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Stack_typeName(self), Stack_stride(self));

        return false;
    }

    Collection *c = (Collection*) self;
    u32 active = StColl_activeCount(c);

    if(active == 0)
        return false;

    bytes stride = Stack_stride(self);

    if(Stack_kind(self) == STACK_KIND_ARRAY) {
        u8 *buf = StColl_data(c);

        if(!buf)
            return false;

        if(dest)
            memcpy(dest, buf + (((bytes) active - 1u) * stride), stride);

        StColl_setActiveCount(c, active - 1u);

        return true;
    }

    if(Stack_kind(self) == STACK_KIND_LINKED) {
        StackNode *top = St_topNode(self);

        if(!top)
            return false;

        if(dest)
            memcpy(dest, stNodeElem(top), stride);

        St_setTopNode(self, StNode_below(top));
        StColl_setActiveCount(c, active - 1u);
        MemoryArena_free(Stack_arena(self), top);

        return true;
    }

    return false;
}

bool Stack_pop(Stack *self)
{
    return Stack_popCopyOut(self, nullptr, 0, nullptr);
}

;;HOTCODE
any Stack_get(const Stack *self, bytes index)
{
    if(!self)
        return nullptr;

    const Collection *c = (const Collection*) self;
    u32 active = StColl_activeCount(c);

    if(index >= (bytes) active)
        return nullptr;

    if(Stack_kind(self) == STACK_KIND_ARRAY) {
        u8 *buf = StColl_data(c);

        if(!buf)
            return nullptr;

        return buf + (index * Stack_stride(self));
    }

    if(Stack_kind(self) == STACK_KIND_LINKED) {
        bytes steps = (bytes) active - 1u - index;
        StackNode *cur = St_topNode(self);

        while(cur && steps > 0) {
            cur = StNode_below(cur);
            steps--;
        }

        if(!cur)
            return nullptr;

        return stNodeElem(cur);
    }

    return nullptr;
}

bool Stack_copyOut(const Stack *self, bytes index, any dest, bytes elemBytes, string typeName)
{
    if(!self || !dest)
        return false;

    if(elemBytes != Stack_stride(self)) {
        THROW("Stack_getAs: width %zu for '%s' does not match '%s' stride %zu",
              elemBytes, typeName ? typeName : "unknown",
              Stack_typeName(self), Stack_stride(self));

        return false;
    }

    any slot = Stack_get(self, index);

    if(!slot)
        return false;

    memcpy(dest, slot, elemBytes);

    return true;
}

fn Stack_clear(Stack *self)
{
    if(!self)
        return;

    Collection *c = (Collection*) self;

    if(Stack_kind(self) == STACK_KIND_LINKED) {
        MemoryArena *arena = Stack_arena(self);
        StackNode *cur = St_topNode(self);

        while(cur) {
            StackNode *below = StNode_below(cur);
            MemoryArena_free(arena, cur);
            cur = below;
        }

        St_setTopNode(self, nullptr);
    }

    StColl_setActiveCount(c, 0);
}

bool Stack_reserve(Stack *self, bytes minCapacity)
{
    if(!self)
        return false;

    if(Stack_kind(self) == STACK_KIND_LINKED)
        return true;

    if(Stack_kind(self) != STACK_KIND_ARRAY)
        return false;

    Collection *c = (Collection*) self;

    if(minCapacity <= (bytes) StColl_capacity(c))
        return true;

    return arrayStackGrow(self, minCapacity) == TRY_OK;
}

;;DECONSTRUCT
fn Stack_free(Stack *self)
{
    if(!self)
        return;

    MemoryArena *arena = Stack_arena(self);
    Collection *c = (Collection*) self;

    if(Stack_kind(self) == STACK_KIND_LINKED) {
        Stack_clear(self);
    } else if(Stack_kind(self) == STACK_KIND_ARRAY) {
        u8 *buf = StColl_data(c);

        if(buf) {
            MemoryArena_free(arena, buf);
            StColl_setData(c, nullptr);
        }
    }

    MemoryArena_free(arena, self);
}

bytes Stack_count(const Stack *self)
{
    if(!self)
        return 0;

    return (bytes) StColl_activeCount((const Collection*) self);
}

bytes Stack_size(const Stack *self)
{
    return Stack_count(self);
}

bytes Stack_length(const Stack *self)
{
    return Stack_count(self);
}

bytes Stack_capacity(const Stack *self)
{
    if(!self)
        return 0;

    if(Stack_kind(self) == STACK_KIND_LINKED)
        return Stack_count(self);

    return (bytes) StColl_capacity((const Collection*) self);
}

bool Stack_isEmpty(const Stack *self)
{
    return Stack_count(self) == 0;
}

bytes Stack_stride(const Stack *self)
{
    if(!self)
        return 0;

    return ElementLayout_stride(Stack_layout(self));
}

string Stack_typeName(const Stack *self)
{
    if(!self)
        return "nullptr";

    return ElementLayout_typeName(Stack_layout(self));
}

string Stack_kindName(const Stack *self)
{
    StackKind k = Stack_kind(self);

    if(k == STACK_KIND_ARRAY)
        return "ArrayStack";

    if(k == STACK_KIND_LINKED)
        return "LinkedStack";

    return "UnknownStack";
}

;;TEST
u64 Stack_exhaustionCount(const Stack *self)
{
    if(!self)
        return 0;

    return Collection_exhaustionCount((const Collection*) self);
}

fn Stack_toString(const Stack *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 needed = snprintf(dest, cap, "Stack(nullptr)");
        formatWrite(dest, cap, outTruncated, needed);

        return;
    }

    i32 needed = snprintf(dest, cap, "%s<%s>(count=%zu, capacity=%zu)",
                          Stack_kindName(self),
                          Stack_typeName(self),
                          Stack_count(self),
                          Stack_capacity(self));
    formatWrite(dest, cap, outTruncated, needed);
}

fn Stack_toStringStruct(const Stack *self, utf8 *dest, bytes cap, bool *outTruncated)
{
    if(!dest || cap == 0) {
        if(outTruncated)
            *outTruncated = true;

        return;
    }

    if(!self) {
        i32 needed = snprintf(dest, cap, "Stack{nullptr}");
        formatWrite(dest, cap, outTruncated, needed);

        return;
    }

    i32 needed = snprintf(dest, cap,
                          "Stack{kind=%s, type=%s, stride=%zu, count=%zu, capacity=%zu, exhausted=%llu}",
                          Stack_kindName(self),
                          Stack_typeName(self),
                          Stack_stride(self),
                          Stack_count(self),
                          Stack_capacity(self),
                          (u64) Stack_exhaustionCount(self));
    formatWrite(dest, cap, outTruncated, needed);
}

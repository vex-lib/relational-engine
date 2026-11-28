#ifndef STRUCT_LINKED_LIST_H
#define STRUCT_LINKED_LIST_H

#include "vex.h"
#include "nio/mem.h"
#include "struct/list.h"

/**
 * struct/linked_list.h, arena-backed doubly-linked implementation of List.
 *
 * Each element lives in an immovable 32-byte-aligned node allocated from the
 * list's owning MemoryArena, so element addresses remain stable across
 * insertions and removals at any index. Construct with any C type directly:
 *
 * List *x = LinkedList(Class);
 * List *y = LinkedList(u32, myArena);
 */

/**
 * 32-byte doubly-linked node header preceding inline element payload bytes.
 *
 * struct LinkedListNode *prev - previous node in list (nullptr at head)
 * struct LinkedListNode *next - next node in list (nullptr at tail)
 * u64 pad0                    - alignment padding so payload sits at offset 32
 * u64 pad1                    - alignment padding so payload sits at offset 32
 */
;;CLASSNAME("LinkedListNode")
typedef struct LinkedListNode {
    struct LinkedListNode *prev;
    struct LinkedListNode *next;
    u64 pad0;
    u64 pad1;
} LinkedListNode;

_Static_assert(sizeof(LinkedListNode) == 32u, "LinkedListNode header must be 32 bytes");

/**
 * Doubly-linked list embedding the List interface at offset zero.
 *
 * List base            - polymorphic List header and active element count
 * LinkedListNode *head - first node in list (nullptr when empty)
 * LinkedListNode *tail - last node in list (nullptr when empty)
 */
;;CLASSNAME("LinkedList")
typedef struct LinkedList {
    List base;
    LinkedListNode *head;
    LinkedListNode *tail;
} LinkedList;

/**
 * Constructs a new empty LinkedList for layout in arena.
 * @param layout validated element layout (use ElementLayout_of(T))
 * @param arena  backing MemoryArena (nullptr defaults to Re_arena())
 * @return owned List* handle backed by LinkedList, or nullptr on refusal
 */
List *LinkedList_newLayout(ElementLayout layout, MemoryArena *arena);

#define LinkedList_1(T)        LinkedList_newLayout(ElementLayout_of(T), Re_arena())
#define LinkedList_2(T, arena) LinkedList_newLayout(ElementLayout_of(T), (arena))
#define LinkedList(...)        CONSTRUCTOR_DISPATCH(LinkedList, __VA_ARGS__)

bool LinkedList_isLinkedList(const List *self);

any LinkedList_get(const List *self, bytes index);
any LinkedList_peekFirst(const List *self);
any LinkedList_peekLast(const List *self);

any LinkedList_emplaceFirst(List *self);
any LinkedList_emplaceLast(List *self);

bool LinkedList_addFirstBytes(List *self, const_any src, bytes elemBytes);
bool LinkedList_addLastBytes(List *self, const_any src, bytes elemBytes);
bool LinkedList_setBytes(List *self, bytes index, const_any src, bytes elemBytes);
bool LinkedList_insertBytes(List *self, bytes index, const_any src, bytes elemBytes);

bool LinkedList_removeFirst(List *self);
bool LinkedList_removeLast(List *self);
bool LinkedList_remove(List *self, bytes index);
fn LinkedList_clear(List *self);
fn LinkedList_free(List *self);

fn LinkedList_toString(const List *self, utf8 *dest, bytes cap, bool *outTruncated);
fn LinkedList_toStringStruct(const List *self, utf8 *dest, bytes cap, bool *outTruncated);

#define LinkedList_add(list, value)                                              \
    ({                                                                           \
        auto _vex_llist_add_val = (value);                                       \
        LinkedList_addLastBytes((list), &_vex_llist_add_val,                     \
                                sizeof(_vex_llist_add_val));                     \
    })

#define LinkedList_addFirst(list, value)                                         \
    ({                                                                           \
        auto _vex_llist_af_val = (value);                                        \
        LinkedList_addFirstBytes((list), &_vex_llist_af_val,                     \
                                 sizeof(_vex_llist_af_val));                     \
    })

#define LinkedList_addLast(list, value)                                          \
    ({                                                                           \
        auto _vex_llist_al_val = (value);                                        \
        LinkedList_addLastBytes((list), &_vex_llist_al_val,                      \
                                sizeof(_vex_llist_al_val));                      \
    })

#define LinkedList_set(list, index, value)                                       \
    ({                                                                           \
        auto _vex_llist_set_val = (value);                                       \
        LinkedList_setBytes((list), (index), &_vex_llist_set_val,                \
                            sizeof(_vex_llist_set_val));                         \
    })

#define LinkedList_insert(list, index, value)                                    \
    ({                                                                           \
        auto _vex_llist_ins_val = (value);                                       \
        LinkedList_insertBytes((list), (index), &_vex_llist_ins_val,             \
                               sizeof(_vex_llist_ins_val));                      \
    })

#define LinkedList_getAs(list, index, T) List_getAs((list), (index), T)

#endif

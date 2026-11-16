/* Owner test for the Stack's explicit fault reporting.
 *
 * Stack_push returns fn for family consistency, but a refused element is now
 * EXPLICIT — never a silent drop:
 *   1. Stack_pushTry returns a named TryCode: TRY_NULL_ARG, TRY_OVERFLOW, or
 *      TRY_NO_MEMORY when the arena cannot grow the buffer; Stack_push emits the
 *      same THROW diagnostic for callers that ignore the Try.
 *   2. the data buffer IS a self-describing arena block — Memory_type() is the
 *      array type id and Memory_length() is exactly capacity * stride.
 *
 * The test deliberately exhausts a small master arena (the Deliberate Exhaustion
 * and Backend Trust Law) so the fault is actually reached, then identifies it by
 * code and proves the stack stays coherent.
 */

#include "vex.h"
#include <stdio.h>
#include <string.h>

#include "exception/try_code.h"
#include "nio/mem.h"
#include "type/re_type.h"
#include "struct/stack.h"

static i32 g_failures = 0;

#define CHECK(name, cond) do { \
    if (cond) { printf("[stackobs] PASS %s\n", name); } \
    else { printf("[stackobs] FAIL %s\n", name); g_failures++; } \
} while (0)

#define ARENA_BYTES (32u * 1024u * 1024u)
#define PUSH_LIMIT  6000000u   // exceeds the 64MB default arena's reachable capacity

i32 main() {
    printf("=== Stack explicit-fault owner suite ===\n");
    // Must run before any other allocation so the small arena wins.
    CHECK("small arena initialized", Memory_init(ARENA_BYTES));

    // The new taxonomy entry is a real, named reason.
    CHECK("TRY_NO_MEMORY has a name", strcmp(TryCode_name(TRY_NO_MEMORY), "TRY_NO_MEMORY") == 0);

    CHECK("TRY_NO_MEMORY has a message", TryCode_message(TRY_NO_MEMORY)[0] != '\0');

    CHECK("TRY_NO_MEMORY is not ok", !TryCode_isOk(TRY_NO_MEMORY));

    Stack *s = Stack_1(ID_INT);

    CHECK("stack constructed", s != nullptr && Stack_stride(s) == 4);

    // --- Explicit rejection: null stack is named, not silent.
    TryValue nullTry = Stack_pushTry(nullptr, 1);

    CHECK("null stack -> TRY_NULL_ARG", TryValue_getCode(&nullTry) == TRY_NULL_ARG);

    // --- 1. The buffer is self-describing: identity and byte size are in-band.
    CHECK("buffer carries the array type", Memory_type(Stack_dataBuffer(s)) == TYPE_INT_ARRAY);

    CHECK("buffer length == capacity * stride",
          Memory_length(Stack_dataBuffer(s)) == (bytes) Stack_capacity(s) * Stack_stride(s));

    // The header tracks every growth, and a good push returns TRY_OK + value.
    for (u64 i = 0; i < 4000u; ++i) {
        TryValue r = Stack_pushTry(s, i);

        if (!TryValue_isOk(&r)) { CHECK("4000 pushes admitted", false); break; }
    }

    CHECK("size advanced after pushes", Stack_size(s) == 4000);

    TryValue okPush = Stack_pushTry(s, 12345);

    CHECK("ok push is TRY_OK", TryValue_isOk(&okPush));

    CHECK("ok push returns the value", TryValue_getValue(&okPush) == 12345);

    CHECK("buffer descriptor tracked growth",
          Memory_length(Stack_dataBuffer(s)) == (bytes) Stack_capacity(s) * Stack_stride(s));

    // --- 2. Exhaust the arena; the fault must be NAMED, not just detected.
    bytes beforeExhaust = Stack_size(s);

    TryCode fault = TRY_OK;

    u64 attempted = 0;

    u64 lastGood = 12345;   // the value admitted by the okPush above

    for (u64 i = 4000u; i < PUSH_LIMIT; ++i) {
        TryValue r = Stack_pushTry(s, i);

        attempted++;

        if (!TryValue_isOk(&r)) { fault = TryValue_getCode(&r); break; }
        lastGood = i;
    }

    CHECK("growth failure was reached", fault != TRY_OK);

    CHECK("fault is TRY_NO_MEMORY (the arena, not bounds)", fault == TRY_NO_MEMORY);

    CHECK("fault names the cause", strcmp(TryCode_name(fault), "TRY_NO_MEMORY") == 0);

    CHECK("rejected push left size unchanged", Stack_size(s) == beforeExhaust + attempted - 1);

    CHECK("capacity descriptor still consistent",
          Memory_length(Stack_dataBuffer(s)) == (bytes) Stack_capacity(s) * Stack_stride(s));

    // --- 3. The stack is still coherent after the named fault.
    bytes top = Stack_size(s);

    CHECK("top element intact", Stack_peek(s) == lastGood);

    CHECK("pop returns the top", Stack_pop(s) == lastGood);

    CHECK("size decrements on pop", Stack_size(s) == top - 1);

    TryValue afterPop = Stack_pushTry(s, 7);   // a freed logical slot needs no growth

    CHECK("pop then push succeeds again", TryValue_isOk(&afterPop));

    Stack_free(s);

    printf("[stackobs] attempted %llu pushes before TRY_NO_MEMORY\n", (u64) attempted);

    if (g_failures != 0) {
        printf("=== %d FAILURES ===\n", g_failures);

        return 1;
    }

    printf("=== ALL STACKOBS PASS ===\n");

    return 0;
}

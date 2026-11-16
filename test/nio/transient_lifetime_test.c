#include "vex.h"
#include "nio/mem.h"
// Relational Engine production transient allocation owner.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Standalone test struct modeling owned vs borrowed text lifetime semantics
typedef struct TestLabel {
    char *text;
    bool ownsText;
} TestLabel;

;;GETTER
static inline char *TestLabel_text(const TestLabel *lbl) { return (*lbl).text; }
;;SETTER
static inline fn TestLabel_setTextField(TestLabel *lbl, char *text) { (*lbl).text = text; }
;;GETTER
static inline bool TestLabel_ownsText(const TestLabel *lbl) { return (*lbl).ownsText; }
;;SETTER
static inline fn TestLabel_setOwnsText(TestLabel *lbl, bool owns) { (*lbl).ownsText = owns; }

/** Allocates and initializes the fallback label object from permanent memory. */
static TestLabel *TestLabel_0() {
    TestLabel *lbl = (TestLabel*) Memory_alloc(100, sizeof(TestLabel));

    if (!lbl) 
        return nullptr;

    TestLabel_setTextField(lbl, nullptr);

    TestLabel_setOwnsText(lbl, false);

    return lbl;
}

/** Replaces label text with an owned permanent copy, releasing prior owned text. */
static fn TestLabel_setText(TestLabel *lbl, const char *text) {
    if (!lbl) 
        return;

    if (TestLabel_text(lbl) && TestLabel_ownsText(lbl)) 
        Memory_free(TestLabel_text(lbl));

    if (text) {
        bytes len = strlen(text) + 1;

        char *copy = (char*) Memory_alloc(101, len);
        TestLabel_setTextField(lbl, copy);

        if (copy) 
            memcpy(copy, text, len);

        TestLabel_setOwnsText(lbl, true);
    } else {
        TestLabel_setTextField(lbl, nullptr);

        TestLabel_setOwnsText(lbl, false);
    }
}

/** Replaces label text with a borrowed pointer without taking ownership. */
static fn TestLabel_setTextBorrowed(TestLabel *lbl, const char *text) {
    if (!lbl) 
        return;
#if defined(DEBUG_BORROW_CHECK)
    if (Transient_contains(text) && !Transient_contains(lbl)) {
        fprintf(stderr, "[LIFETIME ESCAPE] TestLabel_setTextBorrowed: escape abort\n");

        abort();
    }
#endif
    if (TestLabel_text(lbl) && TestLabel_ownsText(lbl)) 
        Memory_free(TestLabel_text(lbl));

    TestLabel_setTextField(lbl, (char*) text);

    TestLabel_setOwnsText(lbl, false);
}

/** Releases owned text and the fallback label allocation. */
static fn TestLabel_free(TestLabel *lbl) {
    if (!lbl) 
        return;

    if (TestLabel_text(lbl) && TestLabel_ownsText(lbl)) 
        Memory_free(TestLabel_text(lbl));

    Memory_free(lbl);
}

#define Label TestLabel
#define Label_0 TestLabel_0
#define Label_setText TestLabel_setText
#define Label_setTextBorrowed TestLabel_setTextBorrowed
#define Label_free TestLabel_free

i32 main() {
    printf("[transient_lifetime_test] Starting lifetime verification suite...\n");

    const char *lit = "literal_constant_string";

    assert(Memory_getLifetime(lit) == MEMORY_LIFETIME_UNKNOWN);

    char stackBuf[64];

    strcpy(stackBuf, "stack_buffer_array");

    assert(Memory_getLifetime(stackBuf) == MEMORY_LIFETIME_UNKNOWN);

    any perm = Memory_alloc(1, 128);

    assert(perm != nullptr);

    assert(Memory_getLifetime(perm) == MEMORY_LIFETIME_PERMANENT);

    any trans = Transient_alloc(2, 128);

    assert(trans != nullptr);

    assert(Memory_getLifetime(trans) == MEMORY_LIFETIME_TRANSIENT);

    Memory_free((any) lit);

    Memory_free((any) stackBuf);

    Memory_free(trans);

    printf("  [PASS] 1. Safe classification & free: lit/stack=UNKNOWN, safe no-op on free\n");

    char *transStr = (char*) Transient_alloc(3, 32);

    strcpy(transStr, "transient_string_123");

    Label *lbl = Label_0();

    assert(lbl != nullptr);

    Label_setText(lbl, transStr);

    assert(TestLabel_ownsText(lbl) == true);

    assert(Memory_getLifetime(TestLabel_text(lbl)) == MEMORY_LIFETIME_PERMANENT);

    Transient_reset();

    assert(strcmp(TestLabel_text(lbl), "transient_string_123") == 0);

    printf("  [PASS] 2. Auto-promote: transient string copied to permanent, survives Transient_reset()\n");

    const char *staticStr = "constant_borrowed_literal";

    Label_setTextBorrowed(lbl, staticStr);

    assert(TestLabel_ownsText(lbl) == false);

    assert(TestLabel_text(lbl) == staticStr);

    Label_setText(lbl, "next_owned_value");

    assert(TestLabel_ownsText(lbl) == true);

    assert(strcmp(TestLabel_text(lbl), "next_owned_value") == 0);

    Label_free(lbl);

    printf("  [PASS] 3. Borrowed no-free: borrowed string is never freed on replacement or teardown\n");

    bytes allocBytes = 65536;

    u8 *p = (u8*) Transient_alloc(4, allocBytes);

    assert(p != nullptr);

    memset(p, 0x55, allocBytes);

    assert((*p) == 0x55);

    Transient_reset();
#if defined(DEBUG_BORROW_CHECK)
    assert((*p) == 0xDD);

    assert(*(p + allocBytes - 1) == 0xDD);

    printf("  [PASS] 4. Poisoning: used transient Bytes poisoned with 0xDD under DEBUG_BORROW_CHECK\n");
#else
    printf("  [PASS] 4. Poisoning: skipped in release (DEBUG_BORROW_CHECK not defined)\n");
#endif

#if defined(DEBUG_BORROW_CHECK)
    pid_t pid = fork();

    assert(pid >= 0);

    if (pid == 0) {
        freopen("/dev/null", "w", stderr);

        char *escapeStr = (char*) Transient_alloc(5, 32);

        strcpy(escapeStr, "escaped_transient");

        Label *escapeLbl = Label_0();

        Label_setTextBorrowed(escapeLbl, escapeStr);

        exit(0);
    }

    i32 status = 0;

    waitpid(pid, &status, 0);

    assert(WIFSIGNALED(status));

    assert(WTERMSIG(status) == SIGABRT);

    printf("  [PASS] 5. Death test: child correctly aborted with SIGABRT on illegal transient borrow\n");
#else
    printf("  [PASS] 5. Death test: skipped in release (DEBUG_BORROW_CHECK not defined)\n");
#endif

    Memory_free(perm);

    printf("[transient_lifetime_test] ALL TESTS PASSED SUCCESSFULLY (exit 0).\n");

    return 0;
}

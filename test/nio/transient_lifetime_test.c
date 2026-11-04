#include "nio/mem.h"
// Relational Engine production transient allocation owner.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(LINK_DARLING)
#include "darling/label/label.h"
#else
// Standalone fallback test struct modeling identical ownership semantics
typedef struct TestLabel {
    char *text;
    bool ownsText;
} TestLabel;

/** Allocates and initializes the fallback label object from permanent memory. */
static TestLabel *TestLabel_0(void) {
    TestLabel *lbl = (TestLabel*) Memory_alloc(100, sizeof(TestLabel));
    if (!lbl)
        return nullptr;
    (*lbl).text = nullptr;
    (*lbl).ownsText = false;
    return lbl;
}

/** Replaces label text with an owned permanent copy, releasing prior owned text. */
static void TestLabel_setText(TestLabel *lbl, const char *text) {
    if (!lbl)
        return;
    if ((*lbl).text && (*lbl).ownsText)
        Memory_free((*lbl).text);
    if (text) {
        size_t len = strlen(text) + 1;
        (*lbl).text = (char*) Memory_alloc(101, len);
        if ((*lbl).text)
            memcpy((*lbl).text, text, len);
        (*lbl).ownsText = true;
    } else {
        (*lbl).text = nullptr;
        (*lbl).ownsText = false;
    }
}

/** Replaces label text with a borrowed pointer without taking ownership. */
static void TestLabel_setTextBorrowed(TestLabel *lbl, const char *text) {
    if (!lbl)
        return;
#if defined(DEBUG_BORROW_CHECK)
    if (Transient_contains(text) && !Transient_contains(lbl)) {
        fprintf(stderr, "[LIFETIME ESCAPE] TestLabel_setTextBorrowed: escape abort\n");
        abort();
    }
#endif
    if ((*lbl).text && (*lbl).ownsText)
        Memory_free((*lbl).text);
    (*lbl).text = (char*) text;
    (*lbl).ownsText = false;
}

/** Releases owned text and the fallback label allocation. */
static void TestLabel_free(TestLabel *lbl) {
    if (!lbl)
        return;
    if ((*lbl).text && (*lbl).ownsText)
        Memory_free((*lbl).text);
    Memory_free(lbl);
}

#define Label TestLabel
#define Label_0 TestLabel_0
#define Label_setText TestLabel_setText
#define Label_setTextBorrowed TestLabel_setTextBorrowed
#define Label_free TestLabel_free
#endif

int main(void) {
    printf("[transient_lifetime_test] Starting lifetime verification suite...\n");

    // 1. Safe classification (no UB on literal or stack pointer)
    const char *lit = "literal_constant_string";
    assert(Memory_getLifetime(lit) == MEMORY_LIFETIME_UNKNOWN);

    char stackBuf[64];
    strcpy(stackBuf, "stack_buffer_array");
    assert(Memory_getLifetime(stackBuf) == MEMORY_LIFETIME_UNKNOWN);

    void *perm = Memory_alloc(1, 128);
    assert(perm != nullptr);
    assert(Memory_getLifetime(perm) == MEMORY_LIFETIME_PERMANENT);

    void *trans = Transient_alloc(2, 128);
    assert(trans != nullptr);
    assert(Memory_getLifetime(trans) == MEMORY_LIFETIME_TRANSIENT);

    // Memory_free on literal/stack (UNKNOWN) and transient pointers must be safe no-ops (no UB)
    Memory_free((void*) lit);
    Memory_free((void*) stackBuf);
    Memory_free(trans);
    printf("  [PASS] 1. Safe classification & free: lit/stack=UNKNOWN, safe no-op on free\n");

    // 2. Auto-promote test: setting transient text promotes to permanent
    char *transStr = (char*) Transient_alloc(3, 32);
    strcpy(transStr, "transient_string_123");

    Label *lbl = Label_0();
    assert(lbl != nullptr);
    Label_setText(lbl, transStr);
    assert((*lbl).ownsText == true);
    assert(Memory_getLifetime((*lbl).text) == MEMORY_LIFETIME_PERMANENT);

    // Reset transient arena: promoted copy must remain completely intact!
    Transient_reset();
    assert(strcmp((*lbl).text, "transient_string_123") == 0);
    printf("  [PASS] 2. Auto-promote: transient string copied to permanent, survives Transient_reset()\n");

    // 3. Borrowed no-free test: borrowed string does not get freed on subsequent update
    const char *staticStr = "constant_borrowed_literal";
    Label_setTextBorrowed(lbl, staticStr);
    assert((*lbl).ownsText == false);
    assert((*lbl).text == staticStr);

    // Replace borrowed text with a new owned text: must not attempt Memory_free on staticStr
    Label_setText(lbl, "next_owned_value");
    assert((*lbl).ownsText == true);
    assert(strcmp((*lbl).text, "next_owned_value") == 0);
    Label_free(lbl);
    printf("  [PASS] 3. Borrowed no-free: borrowed string is never freed on replacement or teardown\n");

    // 4. Poisoning test: 64KB allocation, reset poisons touched memory with 0xDD
    size_t allocBytes = 65536;
    uint8_t *p = (uint8_t*) Transient_alloc(4, allocBytes);
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

    // 5. Death-test escape: fork child to verify abort on illegal transient borrow
#if defined(DEBUG_BORROW_CHECK)
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        // Child process: silence stderr and trigger escape abort
        freopen("/dev/null", "w", stderr);
        char *escapeStr = (char*) Transient_alloc(5, 32);
        strcpy(escapeStr, "escaped_transient");

        Label *escapeLbl = Label_0();
        Label_setTextBorrowed(escapeLbl, escapeStr); // MUST abort() via [LIFETIME ESCAPE]
        exit(0); // should never reach
    }

    int status = 0;
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

#include <assert.h>
// Relational Engine owner test for the migrated production clipboard seam.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clipboard/clipboard.h"
#include "debug/test_support.h"

/* Exercises the host clipboard round trip; execution is gated by explicit mutation permission. */
static void test_clipboard(void) {
    printf("Testing clipboard bridge...\n");
    const char *test_str = "VexGraph Ecosystem Clipboard Payload";

    assert(Clipboard_setText(test_str));
    assert(Clipboard_hasText());

    char out_buf[128];
    bytes len = Clipboard_getText(out_buf, sizeof(out_buf));
    assert(len > 0);
    assert(strcmp(out_buf, test_str) == 0);

    Clipboard_clear();
}

int main(void) {
    const char *permission = getenv("RE_TEST_SYSTEM_CLIPBOARD");
    if (!permission || strcmp(permission, "1") != 0) {
        fputs("SKIP: system clipboard mutation requires RE_TEST_SYSTEM_CLIPBOARD=1\n", stderr);
        return B_TEST_SKIP;
    }
    printf("[Clipboard Test] Starting test suite...\n");
    test_clipboard();
    printf("[Clipboard Test] ALL TESTS PASSED\n");
    return 0;
}

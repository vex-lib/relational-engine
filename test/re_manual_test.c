// Consolidated human-gated test lane for vex/re.
// Requires explicit OS permission (RE_TEST_SYSTEM_CLIPBOARD=1) to mutate the system clipboard.

#include "vex.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clipboard/clipboard.h"
#include "debug/test_support.h"

/** Exercises the host clipboard round trip when system clipboard mutation is permitted. */
static fn suite_clipboard() {
    const char *test_str = "VexGraph Ecosystem Clipboard Payload";
    assert(Clipboard_setText(test_str));
    assert(Clipboard_hasText());

    char out_buf[128];
    bytes len = Clipboard_getText(out_buf, sizeof(out_buf));
    assert(len > 0);
    assert(strcmp(out_buf, test_str) == 0);
    Clipboard_clear();
}

/** Runs human-gated vex/re checks; skips with B_TEST_SKIP unless explicitly opted in. */
i32 main(i32 argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : nullptr;

    if (filter && strcmp(filter, "clipboard") != 0) {
        fprintf(stderr, "re_manual_test: unknown suite '%s'\n", filter);
        return 2;
    }

    const char *permission = getenv("RE_TEST_SYSTEM_CLIPBOARD");
    if (!permission || strcmp(permission, "1") != 0) {
        fputs("SKIP: system clipboard mutation requires RE_TEST_SYSTEM_CLIPBOARD=1\n", stderr);
        return B_TEST_SKIP;
    }

    suite_clipboard();
    puts("re_manual_test: PASS");
    return 0;
}

// Engine-owned bounded frame slot: full buffer, preservation, retry, timeout and
// cancellation. Single-owner API; no concurrent cancellation claim is tested.
// Existing silent rejection policy is retained; THROW/toString gaps remain.
#include "io/frame_inbox.h"
#include "nio/mem.h"
#include <assert.h>
#include <string.h>

int main(void) {
    FrameInbox *a = FrameInbox();
    FrameInbox *b = FrameInbox(UINT64_MAX);
    assert(a && b);
    assert(FrameInbox_getTimeoutNs(b) == FRAME_INBOX_POLL_MAX_NS);
    assert(FrameInbox_getState(a) == FRAME_INBOX_IDLE);
    assert(FrameInbox_getState(nullptr) == FRAME_INBOX_CLOSED);
    assert(FrameInbox_getTimeoutNs(nullptr) == 0);
    assert(FrameInbox_isCancelled(nullptr));
    assert(FrameInbox_getPending(nullptr) == 0);
    FrameInbox_setState(a, FRAME_INBOX_OPEN);
    FrameInbox_setTimeoutNs(a, 1);
    assert(FrameInbox_getState(a) == FRAME_INBOX_OPEN && FrameInbox_getTimeoutNs(a) == 1);
    FrameInbox_setTimeoutNs(a, UINT64_MAX);
    assert(FrameInbox_getTimeoutNs(a) == FRAME_INBOX_POLL_MAX_NS);
    uint8_t source[FRAME_INBOX_RX_CAP], dest[FRAME_INBOX_RX_CAP];
    memset(source, 42, sizeof source);
    assert(!FrameInbox_feed(nullptr, source, 1));
    assert(!FrameInbox_feed(a, nullptr, 1));
    assert(!FrameInbox_feed(a, source, 0));
    assert(!FrameInbox_feed(a, source, UINT32_MAX));
    assert(FrameInbox_feed(a, source, sizeof source));
    assert(!FrameInbox_feed(a, source, 1));
    assert(FrameInbox_getPending(a) == sizeof source);
    uint32_t length = 99;
    memset(dest, 7, sizeof dest);
    assert(!FrameInbox_poll(a, 0, dest, sizeof dest - 1, &length));
    assert(length == 0 && dest[0] == 7 && FrameInbox_getPending(a) == sizeof source);
    assert(!FrameInbox_poll(nullptr, 0, dest, sizeof dest, &length));
    assert(!FrameInbox_poll(a, 0, nullptr, sizeof dest, &length));
    assert(!FrameInbox_poll(a, 0, dest, 0, &length));
    assert(!FrameInbox_poll(a, 0, dest, sizeof dest, nullptr));
    assert(FrameInbox_poll(a, 0, dest, sizeof dest, &length));
    assert(length == sizeof source && memcmp(source, dest, length) == 0);
    assert(!FrameInbox_poll(a, 1, dest, sizeof dest, &length) && length == 0);
    assert(FrameInbox_feed(a, source, 1));
    FrameInbox_cancel(a);
    assert(!FrameInbox_poll(a, UINT64_MAX, dest, sizeof dest, &length));
    assert(FrameInbox_getPending(a) == 1 && FrameInbox_isCancelled(a));
    FrameInbox_setCancelled(a, false);
    assert(FrameInbox_poll(a, 0, dest, sizeof dest, &length) && length == 1);
    assert(FrameInbox_feed(a, source, 1));
    FrameInbox_clear(a);
    assert(FrameInbox_getPending(a) == 0 && FrameInbox_getState(a) == FRAME_INBOX_OPEN);
    FrameInbox_clear(nullptr); FrameInbox_cancel(nullptr);
    FrameInbox_setState(nullptr, FRAME_INBOX_OPEN);
    FrameInbox_setTimeoutNs(nullptr, 1); FrameInbox_setCancelled(nullptr, false);
    FrameInbox_free(a); FrameInbox_free(b); FrameInbox_free(nullptr);
    Memory_freeAll();
    return 0;
}

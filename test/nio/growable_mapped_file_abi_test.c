/* C client of the Rust reservation-backed growing mapping ABI, through the
 * canonical header nio/growable_mapped_file.h. Proves the whole handshake from C,
 * and above all the property the type exists for: a borrowed pointer taken before
 * a growth step is the SAME pointer after it, because the mapping is never remade.
 * Status 8 (no reservation backend on this host) is documented but cannot be
 * exercised here: this host has a backend, so create succeeds.
 * Not a Hotcwap loader test and not a second mapping implementation. */
#define POSIX_C_SOURCE 200809L
#include "relational_engine/growable_mapped_file.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char path[512];
static char otherPath[512];

/* A path that does NOT exist yet: mkstemp reserves a unique name, then we remove it. */
static fn reservePathInto(char *dest, bytes cap) {
    const char *dir = getenv("TMPDIR");
    (fn) snprintf(dest, cap, "%s/re-gmap-XXXXXX", dir != nullptr ? dir : "/tmp");
    int fd = mkstemp(dest);
    assert(fd >= 0);
    assert(close(fd) == 0);
    assert(unlink(dest) == 0);
}

static fn reservePath() { reservePathInto(path, sizeof path); }

int main() {
    ReGrowableMappedFile *owner = nullptr;
    bytes len = 0u, reserved = 0u;
    u64 allocated = 0u;
    bool flag = false;
    u8 dest[64];
    bool truncated = true;

    assert(re_gmap_create(nullptr, 4096u, 0u, &owner) == 1u);
    assert(re_gmap_create("/tmp/x", 4096u, 0u, nullptr) == 1u);
    assert(re_gmap_len(nullptr, &len) == 1u);
    assert(re_gmap_reserved(nullptr, &reserved) == 1u);
    assert(re_gmap_is_open(nullptr, &flag) == 1u);
    assert(re_gmap_allocated_bytes(nullptr, &allocated) == 1u);
    assert(re_gmap_read(nullptr, 0u, 1u, dest, sizeof dest, &truncated) == 1u);
    assert(re_gmap_write(nullptr, 0u, dest, 1u) == 1u);
    assert(re_gmap_borrow(nullptr, nullptr, &len) == 1u);
    assert(re_gmap_grow_to(nullptr, 1u) == 1u);
    assert(re_gmap_flush(nullptr) == 1u);
    assert(re_gmap_sync(nullptr) == 1u);
    assert(re_gmap_close(nullptr) == 1u);
    assert(re_gmap_to_string(nullptr, (char*) dest, sizeof dest, &truncated) == 1u);
    re_gmap_drop(nullptr);

    reservePath();
    int fd = open(path, O_CREAT | O_WRONLY, 0600);
    assert(fd >= 0 && close(fd) == 0);
    assert(re_gmap_create(path, 4096u, 0u, &owner) == 2u);
    u8 untouched[8];
    int check = open(path, O_RDONLY);
    assert(check >= 0);
    assert(read(check, untouched, sizeof untouched) == 0);
    assert(close(check) == 0);
    assert(unlink(path) == 0);

    reservePath();
    assert(re_gmap_create(path, 64u, 65u, &owner) == 3u);
    assert(re_gmap_create(path, 0u, 0u, &owner) == 3u);

    assert(re_gmap_create(path, 4096u, 512u, &owner) == 0u);
    assert(re_gmap_is_open(owner, &flag) == 0u && flag);
    assert(re_gmap_len(owner, &len) == 0u && len == 512u);
    assert(re_gmap_reserved(owner, &reserved) == 0u && reserved == 4096u);
    assert(re_gmap_allocated_bytes(owner, &allocated) == 0u && allocated >= 512u);

    u8 payload[64];
    for (bytes i = 0; i < sizeof payload; ++i)
        payload[i] = (u8) i;
    assert(re_gmap_write(owner, 0u, payload, sizeof payload) == 0u);
    assert(re_gmap_read(owner, 0u, sizeof payload, dest, sizeof dest, &truncated) == 0u);
    assert(memcmp(dest, payload, sizeof payload) == 0);

    assert(re_gmap_write(owner, 512u, payload, 1u) == 3u);
    assert(re_gmap_read(owner, 511u, 2u, dest, sizeof dest, &truncated) == 3u);
    assert(re_gmap_read(owner, (bytes) -1, 2u, dest, sizeof dest, &truncated) == 3u);

    u8 small[4];
    memset(small, 0x5a, sizeof small);
    assert(re_gmap_read(owner, 0u, sizeof payload, small, sizeof small, &truncated) == 7u);
    assert(truncated);
    for (bytes i = 0; i < sizeof small; ++i)
        assert(small[i] == 0x5a);

    const u8 *before = nullptr;
    bytes beforeLen = 0u;
    assert(re_gmap_borrow(owner, &before, &beforeLen) == 0u);
    assert(before != nullptr && beforeLen == 512u);

    assert(re_gmap_grow_to(owner, 2048u) == 0u);
    assert(re_gmap_len(owner, &len) == 0u && len == 2048u);
    assert(re_gmap_allocated_bytes(owner, &allocated) == 0u && allocated >= 2048u);

    const u8 *after = nullptr;
    bytes afterLen = 0u;
    assert(re_gmap_borrow(owner, &after, &afterLen) == 0u);
    assert(after == before);
    assert(afterLen == 2048u);
    assert(memcmp(after, payload, sizeof payload) == 0);

    assert(re_gmap_write(owner, 2000u, (const u8*) "xyz", 3u) == 0u);
    assert(re_gmap_read(owner, 2000u, 3u, dest, sizeof dest, &truncated) == 0u);
    assert(memcmp(dest, "xyz", 3u) == 0);
    assert(re_gmap_grow_to(owner, 4096u) == 0u);
    assert(re_gmap_grow_to(owner, 4097u) == 3u);
    assert(re_gmap_grow_to(owner, 100u) == 3u);
    assert(re_gmap_grow_to(owner, 4096u) == 0u);

    char text[96];
    assert(re_gmap_to_string(owner, text, sizeof text, &truncated) == 0u && !truncated);
    assert(strstr(text, "GrowableMappedFile") != nullptr);
    char tiny[6];
    assert(re_gmap_to_string(owner, tiny, sizeof tiny, &truncated) == 7u && truncated);

    assert(re_gmap_flush(owner) == 0u);
    assert(re_gmap_sync(owner) == 0u);

    assert(re_gmap_close(owner) == 0u);
    assert(re_gmap_is_open(owner, &flag) == 0u && !flag);
    assert(re_gmap_len(owner, &len) == 0u && len == 0u);
    assert(re_gmap_read(owner, 0u, 1u, dest, sizeof dest, &truncated) == 5u);
    assert(re_gmap_borrow(owner, &after, &afterLen) == 5u);
    assert(re_gmap_grow_to(owner, 1u) == 5u);
    assert(re_gmap_flush(owner) == 5u);
    assert(re_gmap_allocated_bytes(owner, &allocated) == 5u);
    assert(re_gmap_close(owner) == 0u);
    re_gmap_drop(owner);

    fd = open(path, O_RDONLY);
    assert(fd >= 0);
    u8 disk[64];
    assert((bytes) read(fd, disk, sizeof disk) == sizeof disk);
    assert(memcmp(disk, payload, sizeof payload) == 0);
    assert(close(fd) == 0);
    assert(unlink(path) == 0);

    reservePath();
    assert(re_gmap_create(path, 256u, 0u, &owner) == 0u);
    assert(re_gmap_is_open(owner, &flag) == 0u && flag);
    assert(re_gmap_len(owner, &len) == 0u && len == 0u);
    assert(re_gmap_read(owner, 0u, 1u, dest, sizeof dest, &truncated) == 3u);
    assert(re_gmap_grow_to(owner, 64u) == 0u);
    assert(re_gmap_write(owner, 63u, (const u8*) "!", 1u) == 0u);
    assert(re_gmap_read(owner, 63u, 1u, dest, sizeof dest, &truncated) == 0u);
    assert(dest[0] == '!');
    re_gmap_drop(owner);
    assert(unlink(path) == 0);

    reservePath();
    int shortFd = open(path, O_CREAT | O_WRONLY, 0600);
    assert(shortFd >= 0);
    assert(write(shortFd, payload, sizeof payload) == (ssize_t) sizeof payload);
    assert(close(shortFd) == 0);
    struct stat st;
    assert(stat(path, &st) == 0);
    off_t shortLength = st.st_size;
    assert(re_gmap_open(path, 65536u, 0u, &owner) == 3u);
    assert(stat(path, &st) == 0 && st.st_size == shortLength);
    assert(unlink(path) == 0);

    reservePath();
    assert(re_gmap_create(path, 65536u, 512u, &owner) == 0u);
    assert(re_gmap_write(owner, 0u, payload, sizeof payload) == 0u);
    assert(re_gmap_sync(owner) == 0u);
    re_gmap_drop(owner);
    assert(stat(path, &st) == 0);
    off_t lengthBefore = st.st_size;

    assert(re_gmap_open(nullptr, 65536u, 0u, &owner) == 1u);
    assert(re_gmap_open(path, 65536u, 0u, nullptr) == 1u);
    assert(re_gmap_open(path, 64u, 65u, &owner) == 3u);
    assert(re_gmap_open(path, 0u, 0u, &owner) == 3u);
    char absent[512];
    (fn) snprintf(absent, sizeof absent, "%s/re-gmap-absent",
                    getenv("TMPDIR") != nullptr ? getenv("TMPDIR") : "/tmp");
    assert(re_gmap_open(absent, 65536u, 0u, &owner) == 2u);
    reservePathInto(otherPath, sizeof otherPath);
    assert(re_gmap_create(otherPath, 65536u, 0u, &owner) == 0u);
    re_gmap_drop(owner);
    assert(re_gmap_open(otherPath, 65536u, 512u, &owner) == 2u);
    assert(unlink(otherPath) == 0);

    assert(re_gmap_open(path, 65536u, 512u, &owner) == 0u);
    assert(stat(path, &st) == 0 && st.st_size == lengthBefore);
    assert(re_gmap_is_open(owner, &flag) == 0u && flag);
    assert(re_gmap_len(owner, &len) == 0u && len == 512u);
    assert(re_gmap_reserved(owner, &reserved) == 0u && reserved == 65536u);
    assert(re_gmap_read(owner, 0u, sizeof payload, dest, sizeof dest, &truncated) == 0u);
    assert(memcmp(dest, payload, sizeof payload) == 0);

    const u8 *reopened = nullptr;
    bytes reopenedLen = 0u;
    assert(re_gmap_borrow(owner, &reopened, &reopenedLen) == 0u);
    assert(re_gmap_allocated_bytes(owner, &allocated) == 0u && allocated >= 512u);
    bytes freeTarget = allocated < 65536u ? (bytes) allocated : 65536u;
    assert(re_gmap_grow_to(owner, freeTarget) == 0u);
    assert(re_gmap_len(owner, &len) == 0u && len == freeTarget);
    u64 afterFree = 0u;
    assert(re_gmap_allocated_bytes(owner, &afterFree) == 0u && afterFree == allocated);
    const u8 *afterRegrow = nullptr;
    bytes afterRegrowLen = 0u;
    assert(re_gmap_borrow(owner, &afterRegrow, &afterRegrowLen) == 0u);
    assert(afterRegrow == reopened && afterRegrowLen == freeTarget);
    assert(memcmp(afterRegrow, payload, sizeof payload) == 0);

    if (freeTarget < 65536u) {
        assert(re_gmap_grow_to(owner, 65536u) == 0u);
        u64 afterGrow = 0u;
        assert(re_gmap_allocated_bytes(owner, &afterGrow) == 0u && afterGrow >= 65536u);
    }

    assert(re_gmap_write(owner, 600u, (const u8*) "reopened", 8u) == 0u);
    assert(re_gmap_sync(owner) == 0u);
    re_gmap_drop(owner);
    assert(re_gmap_open(path, 65536u, 1024u, &owner) == 0u);
    assert(re_gmap_read(owner, 600u, 8u, dest, sizeof dest, &truncated) == 0u);
    assert(memcmp(dest, "reopened", 8u) == 0);
    assert(re_gmap_write(owner, 65536u, payload, 1u) == 3u);
    re_gmap_drop(owner);
    assert(unlink(path) == 0);

    printf("growable_mapped_file_abi_test: PASS\n");
    return 0;
}

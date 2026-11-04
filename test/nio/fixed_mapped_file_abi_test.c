/* C client of the Rust fixed-extent mapping ABI, through the canonical header
 * nio/fixed_mapped_file.h. Proves the whole handshake from C: admission, checked
 * reads, a read-only borrow, a refused short read, write-back, flush/sync, close
 * and drop — plus the refusals that must never half-fill a caller's buffer.
 * Not a Hotcwap loader test, not a second mapping implementation. */
#define POSIX_C_SOURCE 200809L
#include "nio/fixed_mapped_file.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SIZE 32u

static char path[512];

/* Build a regular file of `size` bytes with a known, position-dependent pattern. */
static void makeFile(size_t size) {
    const char *dir = getenv("TMPDIR");
    (void) snprintf(path, sizeof path, "%s/re-mmap-XXXXXX", dir != nullptr ? dir : "/tmp");
    int fd = mkstemp(path);
    assert(fd >= 0);
    uint8_t buffer[SIZE];
    for (size_t i = 0; i < size; ++i)
        buffer[i] = (uint8_t) (i * 7u + 1u);
    if (size > 0u)
        assert((size_t) write(fd, buffer, size) == size);
    assert(close(fd) == 0);
}

static uint8_t expected(size_t index) { return (uint8_t) (index * 7u + 1u); }

int main() {
    ReFixedMappedFile *owner = nullptr;
    size_t len = 0u;
    bool flag = false;
    uint8_t dest[SIZE];
    bool truncated = true;

    /* Argument admission happens before anything reaches the OS. */
    assert(re_mmap_open(nullptr, false, &owner) == 1u);
    assert(re_mmap_open("/tmp", false, nullptr) == 1u);
    assert(re_mmap_len(nullptr, &len) == 1u);
    assert(re_mmap_len(owner, nullptr) == 1u);
    assert(re_mmap_is_open(nullptr, &flag) == 1u);
    assert(re_mmap_is_writable(nullptr, &flag) == 1u);
    assert(re_mmap_read(nullptr, 0u, 1u, dest, sizeof dest, &truncated) == 1u);
    assert(re_mmap_read(owner, 0u, 1u, nullptr, sizeof dest, &truncated) == 1u);
    assert(re_mmap_write(nullptr, 0u, dest, 1u) == 1u);
    assert(re_mmap_borrow(nullptr, nullptr, &len) == 1u);
    assert(re_mmap_flush(nullptr) == 1u);
    assert(re_mmap_sync(nullptr) == 1u);
    assert(re_mmap_close(nullptr) == 1u);
    assert(re_mmap_to_string(nullptr, (char*) dest, sizeof dest, &truncated) == 1u);
    re_mmap_drop(nullptr);   /* quiet no-op */

    /* A missing path is an IO failure; a directory is not a mappable regular file. */
    assert(re_mmap_open("/nonexistent/vexgraph/re-mmap-missing", false, &owner) == 2u);
    assert(re_mmap_open(".", false, &owner) == 6u);

    makeFile(SIZE);

    /* Read-only admission. */
    assert(re_mmap_open(path, false, &owner) == 0u);
    assert(re_mmap_is_open(owner, &flag) == 0u && flag);
    assert(re_mmap_is_writable(owner, &flag) == 0u && !flag);
    assert(re_mmap_len(owner, &len) == 0u && len == SIZE);

    assert(re_mmap_read(owner, 0u, SIZE, dest, sizeof dest, &truncated) == 0u && !truncated);
    for (size_t i = 0; i < SIZE; ++i)
        assert(dest[i] == expected(i));

    /* A sub-range reads exactly its own bytes. */
    assert(re_mmap_read(owner, 4u, 8u, dest, sizeof dest, &truncated) == 0u);
    for (size_t i = 0; i < 8u; ++i)
        assert(dest[i] == expected(i + 4u));

    /* A destination too small refuses with status 7 and leaves it untouched. */
    uint8_t small[4];
    memset(small, 0x5a, sizeof small);
    assert(re_mmap_read(owner, 0u, SIZE, small, sizeof small, &truncated) == 7u);
    assert(truncated);
    for (size_t i = 0; i < sizeof small; ++i)
        assert(small[i] == 0x5a);

    /* Out of range, and an offset+length that would overflow. */
    assert(re_mmap_read(owner, 30u, 8u, dest, sizeof dest, &truncated) == 3u);
    assert(re_mmap_read(owner, (size_t) -1, 2u, dest, sizeof dest, &truncated) == 3u);
    assert(re_mmap_read(owner, SIZE, 0u, dest, sizeof dest, &truncated) == 0u);  /* empty tail is OK */

    /* Borrow: no copy, read-only, dies with the owner. */
    const uint8_t *bytes = nullptr;
    size_t borrowed = 0u;
    assert(re_mmap_borrow(owner, &bytes, &borrowed) == 0u);
    assert(bytes != nullptr && borrowed == SIZE && bytes[0] == expected(0u));
    assert(bytes[SIZE - 1u] == expected(SIZE - 1u));

    /* A read-only admission refuses every mutation, including flush. */
    assert(re_mmap_write(owner, 0u, (const uint8_t*) "x", 1u) == 4u);
    assert(re_mmap_flush(owner) == 4u);

    /* The bounded projection, and its truncation refusal. */
    char text[96];
    assert(re_mmap_to_string(owner, text, sizeof text, &truncated) == 0u && !truncated);
    assert(strstr(text, "FixedMappedFile") != nullptr);
    char tiny[6];
    assert(re_mmap_to_string(owner, tiny, sizeof tiny, &truncated) == 7u && truncated);

    /* Close is not drop: the owner lives on and reports itself closed. */
    assert(re_mmap_close(owner) == 0u);
    assert(re_mmap_is_open(owner, &flag) == 0u && !flag);
    assert(re_mmap_len(owner, &len) == 0u && len == 0u);
    assert(re_mmap_read(owner, 0u, 1u, dest, sizeof dest, &truncated) == 5u);
    assert(re_mmap_borrow(owner, &bytes, &borrowed) == 5u);
    assert(re_mmap_flush(owner) == 5u);
    assert(re_mmap_close(owner) == 0u);   /* repeat safely */
    re_mmap_drop(owner);

    /* Writable round trip: write through the mapping, read it back, then prove the
     * bytes really reached the file. */
    assert(re_mmap_open(path, true, &owner) == 0u);
    assert(re_mmap_is_writable(owner, &flag) == 0u && flag);
    constexpr uint8_t patch[4] = { 0xde, 0xad, 0xbe, 0xef };
    assert(re_mmap_write(owner, 8u, patch, sizeof patch) == 0u);
    assert(re_mmap_read(owner, 8u, 4u, dest, sizeof dest, &truncated) == 0u);
    assert(memcmp(dest, patch, sizeof patch) == 0);
    assert(re_mmap_write(owner, 30u, patch, sizeof patch) == 3u);   /* past the end */
    assert(re_mmap_flush(owner) == 0u);
    assert(re_mmap_sync(owner) == 0u);
    re_mmap_drop(owner);

    uint8_t disk[SIZE];
    int fd = open(path, O_RDONLY);
    assert(fd >= 0);
    assert((size_t) read(fd, disk, sizeof disk) == SIZE);
    assert(close(fd) == 0);
    assert(memcmp(disk + 8, patch, sizeof patch) == 0);

    assert(unlink(path) == 0);

    /* An empty file is a valid open owner with an empty view, not a closed one. */
    makeFile(0u);
    assert(re_mmap_open(path, false, &owner) == 0u);
    assert(re_mmap_is_open(owner, &flag) == 0u && flag);
    assert(re_mmap_len(owner, &len) == 0u && len == 0u);
    assert(re_mmap_borrow(owner, &bytes, &borrowed) == 0u && borrowed == 0u);
    assert(re_mmap_read(owner, 0u, 0u, dest, sizeof dest, &truncated) == 0u);
    assert(re_mmap_read(owner, 0u, 1u, dest, sizeof dest, &truncated) == 3u);
    re_mmap_drop(owner);
    assert(unlink(path) == 0);

    printf("fixed_mapped_file_abi_test: PASS\n");
    return 0;
}

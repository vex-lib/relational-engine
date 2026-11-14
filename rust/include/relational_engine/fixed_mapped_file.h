#ifndef RE_FIXED_MAPPED_FILE_H
#define RE_FIXED_MAPPED_FILE_H

#include "vex.h"

/* Fixed-extent, file-backed byte view owned by Rust, reached from C through this
 * ONE thin extern-C surface. There is no second mapping implementation in C: the
 * owner, the descriptor and the unmapping order live in Rust, and C borrows them.
 *
 * Status: 0 OK, 1 invalid argument, 2 IO/OS failure, 3 bounds/overflow,
 * 4 read-only admission, 5 closed owner, 6 length unrepresentable or not a
 * regular file, 7 read refused because the destination was too small.
 *
 * NOTE the name: this is the FIXED-extent view. It cannot grow — the mapping
 * length is the file's length at open and never changes. A growable sibling is
 * specified separately; do not assume growth here.
 *
 * UNSAFE ADMISSION (the caller's precondition, not checked for you): opening a
 * mutable file-backed mapping is a trust decision. The caller must exclude
 * external mutation and truncation of the file, and any other writable view of
 * it, for the whole lifetime of this owner. Rust borrowing cannot constrain
 * another process. Untrusted mutable files need a snapshot or an enforced
 * ownership boundary instead.
 *
 * LIFETIME: re_mmap_drop is the only release. Close is not drop. re_mmap_borrow
 * returns a pointer INTO the mapping: it stays valid only while this owner is
 * open and no write to an overlapping range occurs, and it dies with the owner.
 * Do not free it, do not keep it past drop, do not assume it survives a close.
 *
 * Not offered: create/truncate/resize, an escaped descriptor, typed
 * reinterpretation, internal dirty tracking, implicit flush or sync,
 * crash-atomic publication, persistence or a database format. Page faults,
 * open, flush, sync and cleanup have no wall-time bound and stay off realtime
 * and bounded-join teardown paths.
 *
 * All calls are externally serialized except that independent readers of an
 * unchanged owner may run concurrently. Emptiness is not closedness: an empty
 * file is a valid open owner whose view is empty.
 *
 * Bounded projections may allocate and follow Rust abort-on-OOM policy. No panic
 * recovery, threads or locks are provided. */

typedef struct ReFixedMappedFile ReFixedMappedFile;

u32 re_mmap_open(const char *path, bool writable, ReFixedMappedFile **out_owner);
fn re_mmap_drop(ReFixedMappedFile *owner);

u32 re_mmap_len(const ReFixedMappedFile *owner, bytes *out_len);
u32 re_mmap_is_open(const ReFixedMappedFile *owner, bool *out_open);
u32 re_mmap_is_writable(const ReFixedMappedFile *owner, bool *out_writable);

u32 re_mmap_read(const ReFixedMappedFile *owner, bytes offset, bytes length,
                      u8 *dest, bytes capacity, bool *out_truncated);
u32 re_mmap_write(ReFixedMappedFile *owner, bytes offset,
                       const u8 *source, bytes length);
u32 re_mmap_borrow(const ReFixedMappedFile *owner,
                        const u8 **out_bytes, bytes *out_len);
u32 re_mmap_flush(ReFixedMappedFile *owner);
u32 re_mmap_sync(ReFixedMappedFile *owner);
u32 re_mmap_close(ReFixedMappedFile *owner);
u32 re_mmap_to_string(const ReFixedMappedFile *owner, char *dest, bytes capacity,
                           bool *out_truncated);

#endif

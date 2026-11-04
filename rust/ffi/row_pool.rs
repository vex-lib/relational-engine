//! DEFINITION: procedural C adapter for Rust RowPool; declarations in row_pool.h.
//! OVERVIEW: new/drop/add/read/write/borrow/remove/release_empty/len/geometry and
//! bounded projections, all re_rows_* exports. No owned adapter class. Integer
//! status is observable rejection (same Rust FFI policy as memory/registry).
//! Geometry/output checks precede dereferences. Live, aligned, disjoint pointers
//! and caller serialization remain preconditions. Wrong-owner handle rejection
//! does not validate a stale owner address. All ordinary allocation paths are
//! fallible; projections are cold and may abort on OOM. No unwind recovery.
use crate::{nio::{row_handle::RowHandle, storage_error::StorageError}, r#struct::row_pool::RowPool};
use std::alloc::{alloc, Layout};

/// Map storage failures to fixed public ABI codes.
fn status(error: StorageError) -> u32 {
    match error {
        StorageError::Allocation | StorageError::Capacity => 2,
        StorageError::Bounds => 3,
        _ => 1,
    }
}

/// # Safety
/// Output is valid/aligned pointer storage. Owner creation publishes only after all allocations succeed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_new(size: usize, alignment: usize, capacity: usize, output: *mut *mut RowPool) -> u32 {
    if output.is_null() { return 1; }
    let pool = match RowPool::new(size, alignment, capacity) { Ok(pool) => pool, Err(error) => return status(error) };
    // SAFETY: nonzero layout of RowPool, allocation will be returned via Box::from_raw.
    let owner = unsafe { alloc(Layout::new::<RowPool>()) }.cast::<RowPool>();
    if owner.is_null() { return 2; }
    unsafe { owner.write(pool); *output = owner; }
    0
}

/// # Safety
/// Null or a live result of new; all borrowers/calls stopped, exactly one drop.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_drop(owner: *mut RowPool) {
    if !owner.is_null() { drop(unsafe { Box::from_raw(owner) }); }
}

/// # Safety
/// Owner exclusive; input/output live, sized, disjoint from owner/rows and each other.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_add(owner: *mut RowPool, source: *const u8, length: usize, output: *mut RowHandle) -> u32 {
    if owner.is_null() || source.is_null() || output.is_null() { return 1; }
    let pool = unsafe { &mut *owner };
    if length != pool.row_size() { return 1; }
    let source = unsafe { std::slice::from_raw_parts(source, length) };
    match pool.add(source) {
        Ok(handle) => { unsafe { *output = handle }; 0 }
        Err(error) => status(error),
    }
}

/// # Safety
/// Owner shared without mutation, writable disjoint dest/flag spans; capacity bounded to isize::MAX.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_read(owner: *const RowPool, handle: RowHandle, dest: *mut u8, capacity: usize, truncated: *mut bool) -> u32 {
    if owner.is_null() || truncated.is_null() || capacity > isize::MAX as usize { return 1; }
    let Some(bytes) = (unsafe { &*owner }).get(handle) else { return 3; };
    if capacity < bytes.len() { unsafe { *truncated = true }; return 4; }
    if dest.is_null() { return 1; }
    unsafe { std::ptr::copy_nonoverlapping(bytes.as_ptr(), dest, bytes.len()); *truncated = false; }
    0
}

/// # Safety
/// Owner exclusive; exact-sized source is live/disjoint from all owner row storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_write(owner: *mut RowPool, handle: RowHandle, source: *const u8, length: usize) -> u32 {
    if owner.is_null() || source.is_null() { return 1; }
    let pool = unsafe { &mut *owner };
    if length != pool.row_size() { return 1; }
    let Some(bytes) = pool.get_mut(handle) else { return 3; };
    unsafe { std::ptr::copy_nonoverlapping(source, bytes.as_mut_ptr(), length); }
    0
}

/// # Safety
/// Owner shared, output valid/disjoint. Returned bytes are read-only; caller obeys borrow lifetime/exclusion.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_borrow(owner: *const RowPool, handle: RowHandle, output: *mut *const u8) -> u32 {
    if owner.is_null() || output.is_null() { return 1; }
    let Some(bytes) = (unsafe { &*owner }).get(handle) else { return 3; };
    unsafe { *output = bytes.as_ptr(); }
    0
}

/// # Safety
/// Owner exclusive; users of the removed row have stopped.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_remove(owner: *mut RowPool, handle: RowHandle) -> u32 {
    if owner.is_null() { return 1; }
    match unsafe { &mut *owner }.remove(handle) { Ok(()) => 0, Err(error) => status(error) }
}

/// # Safety
/// Owner exclusive; output valid/disjoint. Live rows are never freed by this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_release_empty(owner: *mut RowPool, output: *mut usize) -> u32 {
    if owner.is_null() || output.is_null() { return 1; }
    unsafe { *output = (&mut *owner).release_empty_chunks(); }
    0
}

/// # Safety
/// Owner shared without mutation; output valid/disjoint.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_len(owner: *const RowPool, output: *mut usize) -> u32 {
    if owner.is_null() || output.is_null() { return 1; }
    unsafe { *output = (&*owner).len(); }
    0
}

/// # Safety
/// Owner shared; three outputs valid/disjoint from each other/owner.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_geometry(owner: *const RowPool, size: *mut usize, alignment: *mut usize, capacity: *mut usize) -> u32 {
    if owner.is_null() || size.is_null() || alignment.is_null() || capacity.is_null() { return 1; }
    let pool = unsafe { &*owner };
    unsafe { *size = pool.row_size(); *alignment = pool.alignment(); *capacity = pool.rows_per_chunk(); }
    0
}

/// Cold projection adapter; null owner emits nullptr, zero capacity is flagged truncation.
unsafe fn project(owner: *const RowPool, dest: *mut u8, capacity: usize, truncated: *mut bool, structure: bool) -> u32 {
    if truncated.is_null() || capacity > isize::MAX as usize || (dest.is_null() && capacity != 0) { return 1; }
    let dest = if capacity == 0 { &mut [] } else { unsafe { std::slice::from_raw_parts_mut(dest, capacity) } };
    let flag = unsafe { &mut *truncated };
    let complete = if owner.is_null() {
        crate::nio::projection::write("nullptr".to_owned(), dest, flag)
    } else if structure {
        unsafe { &*owner }.to_string_struct(dest, flag)
    } else {
        unsafe { &*owner }.to_string(dest, flag)
    };
    if complete { 0 } else { 4 }
}

/// # Safety
/// Owner null or live/shared; dest and flag writable/disjoint. Formatting is cold only.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_to_string(owner: *const RowPool, dest: *mut u8, capacity: usize, truncated: *mut bool) -> u32 {
    unsafe { project(owner, dest, capacity, truncated, false) }
}

/// # Safety
/// Same pointer/exclusion contract as re_rows_to_string; renders one layer only.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_rows_to_string_struct(owner: *const RowPool, dest: *mut u8, capacity: usize, truncated: *mut bool) -> u32 {
    unsafe { project(owner, dest, capacity, truncated, true) }
}

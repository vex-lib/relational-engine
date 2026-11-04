//! DEFINITION: ffi/memory is the opaque C bridge for the learning backend.
//! OVERVIEW: re_memory_new/drop/copy/read. Status 0=success, 1=invalid,
//! 2=allocation/identity exhaustion, 3=unknown handle, 4=capacity too small.
//! Added status 5=wrong storage kind, 6=byte index bounds, 7=writer busy.
//! Atomic get/set permit concurrent calls while registry topology is frozen;
//! create/copy/drop/release need external exclusion. String output never truncates
//! silently: insufficient capacity sets out_truncated, preserving other outputs.
//! The engine must remain resident while owners or active calls exist.
//! Non-null pointers must be valid, aligned, live and externally synchronized.
//! Inputs/outputs must not alias the owner. read uses memmove semantics.
//! Handles are owner-local; using an ID with another owner is caller misuse.
//! No borrowed block pointer crosses FFI. Errors preserve outputs and content.
//! Null checks cannot establish arbitrary pointer validity. No Vexspoke ABI parity.
use crate::nio::mem::Memory;
use crate::nio::memory_error::MemoryError;

#[unsafe(no_mangle)]
/// Allocate a new opaque memory owner for the C ABI.
pub extern "C" fn re_memory_new() -> *mut Memory {
    // Box allocation follows Rust's process-level OOM policy (may abort).
    Box::into_raw(Box::new(Memory::new()))
}

/// # Safety
/// Owner is null or a live result of re_memory_new, released exactly once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_memory_drop(owner: *mut Memory) {
    if !owner.is_null() { drop(unsafe { Box::from_raw(owner) }); }
}

/// # Safety
/// Owner is live/exclusive; source spans length readable Bytes (or null at zero).
/// Output points to writable u64 storage, disjoint from owner/source.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_memory_copy(
    owner: *mut Memory, source: *const u8, length: usize, output: *mut u64,
) -> u32 {
    if owner.is_null() || output.is_null() || (source.is_null() && length != 0)
        || length > isize::MAX as usize { return 1; }
    let bytes = if length == 0 { &[] } else {
        unsafe { std::slice::from_raw_parts(source, length) }
    };
    match unsafe { &mut *owner }.copy_bytes(bytes) {
        Ok(id) => { unsafe { *output = id }; 0 }
        Err(MemoryError::UnknownHandle) => 3,
        Err(_) => 2,
    }
}

/// # Safety
/// Owner is live/shared; destination spans capacity writable Bytes. Output is
/// writable size_t storage disjoint from destination and owner. No concurrent mutation.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_memory_read(
    owner: *const Memory, id: u64, destination: *mut u8, capacity: usize,
    output_length: *mut usize,
) -> u32 {
    if owner.is_null() || output_length.is_null() || capacity > isize::MAX as usize {
        return 1;
    }
    let bytes = match unsafe { &*owner }.get(id) { Ok(bytes) => bytes, Err(error) => return status(error) };
    if capacity < bytes.len() { return 4; }
    if !bytes.is_empty() {
        if destination.is_null() { return 1; }
        unsafe { std::ptr::copy(bytes.as_ptr(), destination, bytes.len()) };
    }
    unsafe { *output_length = bytes.len() };
    0
}

/// Map a Rust memory error to the stable numeric status used by the C ABI.
fn status(error: MemoryError) -> u32 {
    match error {
        MemoryError::Allocation | MemoryError::Exhausted => 2,
        MemoryError::UnknownHandle => 3,
        MemoryError::Capacity => 4,
        MemoryError::WrongKind => 5,
        MemoryError::Bounds => 6,
        MemoryError::Busy => 7,
    }
}

/// # Safety
/// Same live-pointer/exclusion contract as re_memory_copy; output is disjoint.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_memory_new_atomic_byte(owner: *mut Memory, value: u8, output: *mut u64) -> u32 {
    if owner.is_null() || output.is_null() { return 1; }
    match unsafe { &mut *owner }.new_atomic_byte(value) {
        Ok(id) => { unsafe { *output = id }; 0 }
        Err(error) => status(error),
    }
}

/// # Safety
/// Owner is live/shared; output is writable/disjoint, no concurrent registry mutation.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_memory_get_byte(owner: *const Memory, id: u64, index: usize, output: *mut u8) -> u32 {
    if owner.is_null() || output.is_null() { return 1; }
    match unsafe { &*owner }.get_byte(id, index) {
        Ok(value) => { unsafe { *output = value }; 0 }
        Err(error) => status(error),
    }
}

/// # Safety
/// Owner is live/shared; output is writable/disjoint; registry topology stays frozen.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_memory_get_atomic_byte(owner: *const Memory, id: u64, output: *mut u8) -> u32 {
    if owner.is_null() || output.is_null() { return 1; }
    match unsafe { &*owner }.get_atomic_byte(id) {
        Ok(value) => { unsafe { *output = value }; 0 }
        Err(error) => status(error),
    }
}

/// # Safety
/// Owner is live/shared; no concurrent registry mutation/destruction.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_memory_set_atomic_byte(owner: *const Memory, id: u64, value: u8) -> u32 {
    if owner.is_null() { return 1; }
    match unsafe { &*owner }.set_atomic_byte(id, value) { Ok(()) => 0, Err(error) => status(error) }
}

/// # Safety
/// As re_memory_copy. Retention limit counts Bytes plus snapshot record costs.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_memory_new_atomic_string(
    owner: *mut Memory, source: *const u8, length: usize, retention_limit: usize, output: *mut u64,
) -> u32 {
    if owner.is_null() || output.is_null() || (source.is_null() && length != 0)
        || length > isize::MAX as usize { return 1; }
    let source = if length == 0 { &[] } else { unsafe { std::slice::from_raw_parts(source, length) } };
    match unsafe { &mut *owner }.new_atomic_string(source, retention_limit) {
        Ok(id) => { unsafe { *output = id }; 0 }
        Err(error) => status(error),
    }
}

/// # Safety
/// Owner is live/shared and topology is frozen; source is a readable length-byte
/// span or null at zero. Set may allocate; returns busy rather than waiting.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_memory_set_atomic_string(owner: *const Memory, id: u64, source: *const u8, length: usize) -> u32 {
    if owner.is_null() || (source.is_null() && length != 0) || length > isize::MAX as usize { return 1; }
    let source = if length == 0 { &[] } else { unsafe { std::slice::from_raw_parts(source, length) } };
    match unsafe { &*owner }.set_atomic_string(id, source) { Ok(()) => 0, Err(error) => status(error) }
}

/// # Safety
/// As re_memory_read, plus writable/disjoint out_truncated. Atomic string gets
/// copy one immutable snapshot, not a raw Rust atomic or dangling borrowed pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_memory_get_atomic_string(
    owner: *const Memory, id: u64, destination: *mut u8, capacity: usize,
    output_length: *mut usize, out_truncated: *mut bool,
) -> u32 {
    if owner.is_null() || output_length.is_null() || out_truncated.is_null()
        || capacity > isize::MAX as usize { return 1; }
    let bytes = match unsafe { &*owner }.get_atomic_string(id) { Ok(value) => value, Err(error) => return status(error) };
    if capacity < bytes.len() { unsafe { *out_truncated = true }; return 4; }
    if !bytes.is_empty() {
        if destination.is_null() { return 1; }
        unsafe { std::ptr::copy(bytes.as_ptr(), destination, bytes.len()) };
    }
    unsafe { *output_length = bytes.len(); *out_truncated = false; }
    0
}

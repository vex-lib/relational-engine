//! DEFINITION: opaque C registry owner plus borrowed repr(C) VariableSlot view.
//! OVERVIEW: re_variables_new/drop/add/find/slot/set_pointer; declarations in
//! relational_engine/variable_registry.h. No separate Rust-owned class here.
//! 0 success, 1 invalid/name/layout, 2 allocation/capacity, 3 missing/bounds,
//! 4 duplicate. Errors preserve caller outputs and existing bindings.
//! Every API needs external exclusion of mutation/drop and valid, disjoint spans.
//! Null drop is safe; null other owner/output rejects. Arbitrary pointer/stale
//! owner validation is not promised. Returned slot pointers survive growth until
//! drop. Value addresses are opaque/borrowed, never dereferenced or freed here.
use crate::variable::variable_registry::VariableRegistry;
use crate::variable::variable_slot::VariableSlot;
use crate::nio::storage_error::StorageError;
use crate::variable::variable_slot::VARIABLE_NAME_MAX;

/// Map a registry storage error to the stable numeric status used by the C ABI.
fn status(error: StorageError) -> u32 {
    match error {
        StorageError::Layout | StorageError::InvalidName => 1,
        StorageError::Allocation | StorageError::Capacity => 2,
        StorageError::Bounds => 3,
        StorageError::Duplicate => 4,
    }
}

/// # Safety
/// out_owner must be null or valid/aligned writable pointer storage.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_variables_new(rows_per_chunk: usize, out_owner: *mut *mut VariableRegistry) -> u32 {
    if out_owner.is_null() { return 1; }
    match VariableRegistry::new(rows_per_chunk) {
        Ok(owner) => { unsafe { *out_owner = Box::into_raw(Box::new(owner)) }; 0 }
        Err(error) => status(error),
    }
}

/// # Safety
/// owner is null or a live new result, released exactly once with all users stopped.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_variables_drop(owner: *mut VariableRegistry) {
    if !owner.is_null() { drop(unsafe { Box::from_raw(owner) }); }
}

/// # Safety
/// owner live/exclusive; name readable for name_bytes; output writable/aligned,
/// disjoint from input/owner. value_pointer is opaque and retained by caller.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_variables_add(owner: *mut VariableRegistry, name: *const u8,
    name_bytes: usize, value_pointer: *const u8, out_index: *mut usize) -> u32 {
    if owner.is_null() || name.is_null() || out_index.is_null() || name_bytes > VARIABLE_NAME_MAX { return 1; }
    let name = unsafe { std::slice::from_raw_parts(name, name_bytes) };
    match unsafe { &mut *owner }.add(name, value_pointer) {
        Ok(index) => { unsafe { *out_index = index }; 0 }
        Err(error) => status(error),
    }
}

/// # Safety
/// owner live/shared without mutation; name readable; output writable/aligned and
/// disjoint from owner/name. No concurrent drop, rebinding or add.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_variables_find(owner: *const VariableRegistry, name: *const u8,
    name_bytes: usize, out_index: *mut usize) -> u32 {
    if owner.is_null() || name.is_null() || out_index.is_null() || name_bytes > VARIABLE_NAME_MAX { return 1; }
    let name = unsafe { std::slice::from_raw_parts(name, name_bytes) };
    match unsafe { &*owner }.find(name) {
        Ok(Some(index)) => { unsafe { *out_index = index }; 0 }
        Ok(None) => 3,
        Err(error) => status(error),
    }
}

/// # Safety
/// owner live/shared; output writable/aligned and disjoint from owner. The returned
/// read-only row is borrowed until owner drop; exclude readers during rebinding.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_variables_slot(owner: *const VariableRegistry, index: usize,
    out_slot: *mut *const VariableSlot) -> u32 {
    if owner.is_null() || out_slot.is_null() { return 1; }
    match unsafe { &*owner }.get(index) {
        Some(slot) => { unsafe { *out_slot = slot }; 0 }
        None => 3,
    }
}

/// # Safety
/// owner live/exclusive, including exclusion of C reads through borrowed slot views.
/// The pointed value is opaque; its own lifetime/type/access rules belong to caller.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn re_variables_set_pointer(owner: *mut VariableRegistry, index: usize,
    value_pointer: *const u8) -> u32 {
    if owner.is_null() { return 1; }
    match unsafe { &mut *owner }.set_pointer(index, value_pointer) {
        Ok(()) => 0,
        Err(error) => status(error),
    }
}

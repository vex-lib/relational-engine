//! FFI owner: deterministic constructor-owner OOM and output preservation.
//! The real C client owns every export's normal/invalid behavior. Raw non-live
//! pointers are outside contract; no concurrent mutation is advertised.
#[path = "../fail_allocator.rs"] mod faults;
use relational_engine_scratchpad::{RowPool, RowHandle, ffi::row_pool::*};

#[test]
fn owner_allocation_and_add_failure_preserve_outputs() {
    let mut owner: *mut RowPool = std::ptr::null_mut();
    faults::fail_after(0);
    let failed = unsafe { re_rows_new(8, 8, 1, &mut owner) };
    faults::reset();
    assert_eq!(failed, 2);
    assert!(owner.is_null());
    assert_eq!(unsafe { re_rows_new(8, 8, 1, &mut owner) }, 0);
    for stage in 0..5 {
        let mut handle = RowHandle::zero();
        faults::fail_after(stage);
        let failed = unsafe { re_rows_add(owner, [1u8; 8].as_ptr(), 8, &mut handle) };
        faults::reset();
        assert_eq!(failed, 2);
        assert!(handle.is_zero());
        let mut len = usize::MAX;
        assert_eq!(unsafe { re_rows_len(owner, &mut len) }, 0);
        assert_eq!(len, 0);
        // Drop recreates a fresh owner so each stage includes the directory allocation.
        unsafe { re_rows_drop(owner); }
        owner = std::ptr::null_mut();
        assert_eq!(unsafe { re_rows_new(8, 8, 1, &mut owner) }, 0);
    }
    let mut handle = RowHandle::zero();
    assert_eq!(unsafe { re_rows_add(owner, [2u8; 8].as_ptr(), 8, &mut handle) }, 0);
    let mut bytes = [0; 8];
    let mut truncated = true;
    assert_eq!(unsafe { re_rows_read(owner, handle, bytes.as_mut_ptr(), 8, &mut truncated) }, 0);
    assert_eq!(bytes, [2; 8]);
    assert!(!truncated);
    unsafe { re_rows_drop(owner); }
}

//! Owner proof of all registry extern forms and preserved outputs on rejection.
use relational_engine_scratchpad::{VariableRegistry, VariableSlot, ffi::*};

/// Exercises registry FFI admission, error codes, preserved outputs, pointer updates, and cleanup.
#[test]
fn registry_ffi_rejections_and_recovery() {
    unsafe {
        let mut owner: *mut VariableRegistry = std::ptr::null_mut();
        assert_eq!(re_variables_new(0, &mut owner), 1);
        assert!(owner.is_null());
        assert_eq!(re_variables_new(2, std::ptr::null_mut()), 1);
        assert_eq!(re_variables_new(2, &mut owner), 0);
        let target = 8u8;
        let mut index = 99;
        let mut slot: *const VariableSlot = std::ptr::null();
        assert_eq!(re_variables_add(owner, b"gold".as_ptr(), 4, &target, &mut index), 0);
        assert_eq!(index, 0);
        assert_eq!(re_variables_slot(owner, 0, &mut slot), 0);
        assert_eq!((*slot).get_pointer(), &target as *const u8);
        for (name, length) in [(b"x".as_ptr(), usize::MAX), (b"".as_ptr(), 0), (b"a\0b".as_ptr(), 3), (std::ptr::null(), 1)] {
            index = 99;
            assert_eq!(re_variables_add(owner, name, length, std::ptr::null(), &mut index), 1);
            assert_eq!(index, 99);
            assert_eq!(re_variables_find(owner, name, length, &mut index), 1);
            assert_eq!(index, 99);
        }
        assert_eq!(re_variables_add(owner, b"gold".as_ptr(), 4, std::ptr::null(), &mut index), 4);
        assert_eq!(index, 99);
        assert_eq!((*slot).get_pointer(), &target as *const u8);
        assert_eq!(re_variables_find(owner, b"none".as_ptr(), 4, &mut index), 3);
        assert_eq!(index, 99);
        let first = slot;
        assert_eq!(re_variables_slot(owner, usize::MAX, &mut slot), 3);
        assert_eq!(slot, first);
        assert_eq!(re_variables_slot(owner, 0, std::ptr::null_mut()), 1);
        assert_eq!(re_variables_slot(std::ptr::null(), 0, &mut slot), 1);
        assert_eq!(re_variables_add(std::ptr::null_mut(), b"x".as_ptr(), 1, std::ptr::null(), &mut index), 1);
        assert_eq!(re_variables_add(owner, b"x".as_ptr(), 1, std::ptr::null(), std::ptr::null_mut()), 1);
        assert_eq!(re_variables_find(std::ptr::null(), b"x".as_ptr(), 1, &mut index), 1);
        assert_eq!(re_variables_find(owner, b"x".as_ptr(), 1, std::ptr::null_mut()), 1);
        assert_eq!(re_variables_set_pointer(std::ptr::null_mut(), 0, std::ptr::null()), 1);
        assert_eq!(re_variables_set_pointer(owner, usize::MAX, std::ptr::null()), 3);
        assert_eq!(re_variables_set_pointer(owner, 0, std::ptr::null()), 0);
        assert!((*slot).get_pointer().is_null());
        assert_eq!(re_variables_find(owner, b"GOLD".as_ptr(), 4, &mut index), 0);
        assert_eq!(index, 0);
        re_variables_drop(owner);
        re_variables_drop(std::ptr::null_mut());
        assert_eq!(target, 8);
    }
}

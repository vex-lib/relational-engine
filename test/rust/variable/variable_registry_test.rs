//! Registry uses actual linked C search, owns labels and borrows values.
//! Surface/arity, duplicates, invalid names, chunk growth, stable addresses,
//! missing/bounds, OOM retry and teardown preserve external value ownership.
#[path = "../fail_allocator.rs"] mod faults;
use relational_engine_scratchpad::{VariableRegistry, VariableSlot, StorageError};

/// Verifies native name search, stable bindings, growth, rejection preservation, and borrowed-value lifetime.
#[test]
fn native_search_and_stable_bindings() {
    assert!(VariableRegistry!().unwrap().is_empty());
    assert!(VariableRegistry::zero().unwrap().is_empty());
    assert!(matches!(VariableRegistry::new(0), Err(StorageError::Layout)));
    let mut registry = VariableRegistry!(2).unwrap();
    assert_eq!(registry.find(b"absent"), Ok(None));
    let value = Box::new(42u8);
    let pointer = &*value as *const u8;
    faults::fail_after(0);
    assert_eq!(registry.add(b"Gold", pointer), Err(StorageError::Allocation));
    assert!(registry.is_empty());
    assert_eq!(registry.add(b"Gold", pointer), Ok(0));
    let first = registry.get(0).unwrap() as *const VariableSlot;
    for index in 1..257 {
        let name = format!("field{index}");
        assert_eq!(registry.add(name.as_bytes(), std::ptr::null()), Ok(index));
    }
    assert_eq!(registry.get(0).unwrap() as *const VariableSlot, first);
    assert_eq!(unsafe { (*first).get_pointer() }, pointer);
    assert_eq!(registry.find(b"GOLD"), Ok(Some(0)));
    assert_eq!(registry.find(b"field256"), Ok(Some(256)));
    assert_eq!(registry.find(b"missing"), Ok(None));
    assert_eq!(registry.add(b"gold", std::ptr::null()), Err(StorageError::Duplicate));
    assert_eq!(registry.len(), 257);
    assert_eq!(registry.get(0).unwrap().get_pointer(), pointer);
    // Fill the partial leaf, then fail the next leaf allocation with existing rows.
    assert_eq!(registry.add(b"last", std::ptr::null()), Ok(257));
    faults::fail_after(0);
    assert_eq!(registry.add(b"retry", std::ptr::null()), Err(StorageError::Allocation));
    assert_eq!(registry.len(), 258);
    assert_eq!(registry.find(b"retry"), Ok(None));
    assert_eq!(registry.add(b"retry", std::ptr::null()), Ok(258));
    for name in [b"".as_slice(), b"a..b", b"\0", b"\xff", b"123456789012345678901234"] {
        assert_eq!(registry.add(name, pointer), Err(StorageError::InvalidName));
        assert_eq!(registry.find(name), Err(StorageError::InvalidName));
        assert_eq!(registry.len(), 259);
    }
    assert!(registry.get(259).is_none());
    assert_eq!(registry.set_pointer(usize::MAX, pointer), Err(StorageError::Bounds));
    registry.set_pointer(0, std::ptr::null()).unwrap();
    assert!(registry.get(0).unwrap().get_pointer().is_null());
    assert_eq!(registry.find(b"gold"), Ok(Some(0)));
    let mut dest = [0; 256];
    let mut truncated = true;
    assert!(registry.to_string(&mut dest, &mut truncated) && !truncated);
    assert!(registry.to_string_struct(&mut dest, &mut truncated));
    assert!(!registry.to_string_struct(&mut [], &mut truncated) && truncated);
    drop(registry);
    assert_eq!(*value, 42); // Borrowed target was not freed by registry teardown.
    faults::reset();
}

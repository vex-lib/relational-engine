//! VariableSlot is a byte name and borrowed opaque pointer, not a value owner.
//! Full surface/arity, fixed layout, byte boundaries, rejection atomicity,
//! binding roundtrips and bounded projections. Arbitrary pointers are never read.
use relational_engine_scratchpad::{VariableSlot, StorageError};

/// Checks the fixed slot layout, accepted names, atomic rejection, borrowed pointer storage, and projections.
#[test]
fn slot_layout_and_names() {
    assert_eq!(std::mem::size_of::<VariableSlot>(), 32);
    assert_eq!(std::mem::align_of::<VariableSlot>(), 8);
    let empty = VariableSlot!();
    assert!(empty.is_empty() && empty.get_pointer().is_null());
    assert_eq!(empty.get_name_bytes(), &[0; 24]);
    let target = 72u8;
    let pointer = &target as *const u8;
    let mut slot = VariableSlot!(b"Character.Position.X", pointer).unwrap();
    assert_eq!(slot.get_name(), b"character.position.x");
    assert_eq!(slot.get_pointer(), pointer);
    assert_eq!(slot.get_name_bytes()[23], 0);
    let before = *slot.get_name_bytes();
    for name in [b"".as_slice(), b"a\0b", b".a", b"a.", b"a..b", b"a b", b"a/b", b"\xff", b"123456789012345678901234"] {
        assert_eq!(slot.set_name(name), Err(StorageError::InvalidName));
        assert_eq!(slot.get_name_bytes(), &before);
        assert_eq!(slot.get_pointer(), pointer);
        assert!(matches!(VariableSlot::new(name, pointer), Err(StorageError::InvalidName)));
    }
    for byte in 0..=255u8 {
        let accepted = byte.is_ascii_alphanumeric() || b"_$-".contains(&byte);
        assert_eq!(VariableSlot::new(&[byte], std::ptr::null()).is_ok(), accepted);
    }
    slot.set_name(b"12345678901234567890123").unwrap();
    assert_eq!(slot.get_name().len(), 23);
    slot.set_name(b"X").unwrap();
    assert_eq!(slot.get_name_bytes(), b"x\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0");
    slot.set_pointer(std::ptr::null());
    assert!(slot.get_pointer().is_null());
    let opaque = std::ptr::without_provenance::<u8>(1);
    slot.set_pointer(opaque); // Stored, not dereferenced or claimed valid.
    assert_eq!(slot.get_pointer(), opaque);
    assert!(VariableSlot!(b"a").unwrap().get_pointer().is_null());
    assert!(VariableSlot::zero().is_empty());
    let mut dest = [0; 128];
    let mut truncated = true;
    assert!(slot.to_string(&mut dest, &mut truncated) && !truncated);
    assert!(slot.to_string_struct(&mut dest, &mut truncated));
    assert!(!slot.to_string(&mut [], &mut truncated) && truncated);
    let mut tiny = [255; 1];
    assert!(!slot.to_string_struct(&mut tiny, &mut truncated) && truncated);
    assert_eq!(tiny, [0]);
    assert_eq!(target, 72);
}

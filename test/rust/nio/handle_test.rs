//! Owns every Handle public form: construction, accessors, the zero sentinel and
//! bounded string projections. A Handle is a generation-tagged identity; the owning
//! TypedChunk/TypedPool is what validates it against a live slot.
use relational_engine_scratchpad::Handle;

/// Proves construction, accessors, the zero sentinel and string projections.
#[test]
fn handle_construction_and_projections() {
    let zero = Handle::zero();
    assert!(zero.is_zero());
    assert_eq!(zero.index(), 0);
    assert_eq!(zero.generation(), 0);

    let handle = Handle::new(7, 3);
    assert!(!handle.is_zero());
    assert_eq!(handle.index(), 7);
    assert_eq!(handle.generation(), 3);
    assert_eq!(handle, Handle::new(7, 3));
    assert_ne!(handle, Handle::new(7, 4));

    let mut dest = [0u8; 64];
    let mut truncated = true;
    assert!(handle.to_string(&mut dest, &mut truncated) && !truncated);
    assert_eq!(&dest[..29], b"Handle(index=7, generation=3)");
    let mut struct_dest = [0u8; 64];
    assert!(handle.to_string_struct(&mut struct_dest, &mut truncated));
    assert!(!handle.to_string(&mut [], &mut truncated) && truncated);
    assert!(!handle.to_string_struct(&mut [0u8; 1], &mut truncated) && truncated);
}

//! RowHandle owner: zero, constructor macro, projections and exact C ABI record layout.
use relational_engine_scratchpad::{RowHandle, RowPool};

#[test]
fn identity_layout_and_projections() {
    assert_eq!(std::mem::size_of::<RowHandle>(), 24);
    assert_eq!(std::mem::align_of::<RowHandle>(), 8);
    let zero = RowHandle!();
    assert!(zero.is_zero());
    let h = RowHandle!(7, 4, 2);
    assert_eq!((h.owner(), h.index(), h.generation()), (7, 4, 2));
    assert_ne!(h, zero);
    let pool = RowPool!(8, 8, 1).unwrap();
    assert!(pool.get(h).is_none() && pool.get(zero).is_none());
    let mut dest = [0; 160];
    let mut truncated = true;
    assert!(h.to_string(&mut dest, &mut truncated) && !truncated);
    assert!(h.to_string_struct(&mut dest, &mut truncated) && !truncated);
    assert!(!h.to_string(&mut [], &mut truncated) && truncated);
    assert!(!h.to_string_struct(&mut [0; 1], &mut truncated) && truncated);
}

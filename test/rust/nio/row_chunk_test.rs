//! RowChunk owner: actual aligned flat bytes, stable borrows, stale/zero rejection,
//! reclaim/recreate history and every allocation failure stage. No concurrent mutation.
#[path = "../fail_allocator.rs"] mod faults;
use relational_engine_scratchpad::{RowChunk, Handle, StorageError};

#[test]
fn geometry_flat_rows_reclaim_and_failures() {
    for geometry in [(0, 8, 1), (8, 0, 1), (8, 3, 1), (8, 8, 0), (usize::MAX, 8, 1), (8, 8, usize::MAX)] {
        assert!(matches!(RowChunk::new(geometry.0, geometry.1, geometry.2), Err(StorageError::Layout)));
    }
    for stage in 0..4 {
        faults::fail_after(stage);
        let rejected = RowChunk!(3, 256, 2);
        faults::reset();
        assert!(matches!(rejected, Err(StorageError::Allocation)));
    }
    let mut chunk = RowChunk!(3, 256, 2).unwrap();
    assert!(chunk.is_empty() && chunk.has_backing());
    assert_eq!(chunk.add(&[]), Err(StorageError::Layout));
    let a = chunk.add(&[1, 0, 255]).unwrap();
    let b = chunk.add(&[4, 5, 6]).unwrap();
    let pa = chunk.get(a).unwrap().as_ptr();
    let pb = chunk.get(b).unwrap().as_ptr();
    assert_eq!(pa as usize % 256, 0);
    assert_eq!(pb as usize - pa as usize, 256);
    assert!(chunk.is_full());
    assert_eq!(chunk.add(&[7, 8, 9]), Err(StorageError::Capacity));
    assert!(!chunk.release_empty());
    assert!(chunk.get(Handle::zero()).is_none());
    chunk.get_mut(b).unwrap()[0] = 42;
    assert_eq!(chunk.get(b), Some(&[42, 5, 6][..]));
    chunk.remove(a).unwrap();
    assert!(chunk.get(a).is_none());
    assert_eq!(chunk.remove(a), Err(StorageError::Bounds));
    assert_eq!(chunk.get(b).unwrap().as_ptr(), pb);
    chunk.remove(b).unwrap();
    assert!(chunk.release_empty() && !chunk.has_backing());
    assert!(!chunk.release_empty());
    for stage in 0..2 {
        faults::fail_after(stage);
        let failed = chunk.add(&[8, 0, 9]);
        faults::reset();
        assert_eq!(failed, Err(StorageError::Allocation));
        assert!(chunk.get(a).is_none() && chunk.is_empty());
        chunk.release_empty();
    }
    let fresh = chunk.add(&[8, 0, 9]).unwrap();
    assert_ne!(fresh, a);
    assert!(chunk.get(a).is_none() && chunk.get(b).is_none());
    let mut dest = [0; 300];
    let mut cut = true;
    assert!(chunk.to_string(&mut dest, &mut cut) && !cut);
    assert!(chunk.to_string_struct(&mut dest, &mut cut) && !cut);
    assert!(!chunk.to_string(&mut [], &mut cut) && cut);
    assert!(!chunk.to_string_struct(&mut [0; 1], &mut cut) && cut);
}

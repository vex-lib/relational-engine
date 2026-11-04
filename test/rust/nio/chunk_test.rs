//! Chunk owns stable typed rows. Proves all constructors, geometry admission,
//! full rejection, OOM/retry, over-alignment, drop exactly once and projections.
//! Mutation is exclusive; stale/arbitrary raw-pointer dereference is not promised.
#[path = "../fail_allocator.rs"] mod faults;
use relational_engine_scratchpad::{Chunk, StorageError};
use std::{cell::Cell, rc::Rc};
#[repr(align(256))]
struct Aligned(u64);
struct Tracked(Rc<Cell<usize>>);
impl Drop for Tracked { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }

/// Tests stable typed-row addresses, geometry failures, capacity, alignment, drop ownership, and strings.
#[test]
fn stable_chunk_contract() {
    assert!(Chunk!(u64).unwrap().is_empty());
    assert!(Chunk::<u64>::zero().unwrap().is_empty());
    assert!(matches!(Chunk::<u64>::new(0), Err(StorageError::Layout)));
    assert!(matches!(Chunk::<()>::new(1), Err(StorageError::Layout)));
    for size in [usize::MAX, isize::MAX as usize] {
        assert!(matches!(Chunk::<u64>::new(size), Err(StorageError::Layout)));
    }
    faults::fail_after(0);
    assert!(matches!(Chunk::<u64>::new(4), Err(StorageError::Allocation)));
    let mut rows = Chunk!(u64, 2).unwrap();
    assert_eq!(rows.get_capacity(), 2);
    assert_eq!(rows.add(11), Ok(0));
    let address = rows.get(0).unwrap() as *const u64;
    assert_eq!(rows.add(22), Ok(1));
    assert_eq!(rows.add(33), Err(StorageError::Capacity));
    assert_eq!(rows.as_slice(), &[11, 22]);
    assert_eq!(rows.len(), 2);
    assert!(!rows.is_empty());
    assert_eq!(rows.get(0).unwrap() as *const u64, address);
    *rows.get_mut(0).unwrap() = 44;
    assert_eq!(unsafe { *address }, 44);
    assert!(rows.get(2).is_none() && rows.get_mut(usize::MAX).is_none());
    let moved = rows;
    assert_eq!(moved.get(0).unwrap() as *const u64, address);
    let mut aligned = Chunk!(Aligned, 1).unwrap();
    aligned.add(Aligned(5)).unwrap();
    let row = aligned.get(0).unwrap();
    assert_eq!((row as *const Aligned as usize) % 256, 0);
    assert_eq!(row.0, 5);
    let drops = Rc::new(Cell::new(0));
    {
        let mut tracked = Chunk!(Tracked, 1).unwrap();
        tracked.add(Tracked(drops.clone())).unwrap();
        assert_eq!(tracked.add(Tracked(drops.clone())), Err(StorageError::Capacity));
        assert_eq!(drops.get(), 1); // Rejected input dropped, owned row untouched.
    }
    assert_eq!(drops.get(), 2);
    let mut dest = [0; 128];
    let mut truncated = true;
    assert!(moved.to_string(&mut dest, &mut truncated) && !truncated);
    let text = std::str::from_utf8(&dest[..dest.iter().position(|b| *b == 0).unwrap()]).unwrap();
    assert_eq!(text, "Chunk(len=2, capacity=2)");
    let mut exact = vec![0; text.len() + 1];
    assert!(moved.to_string(&mut exact, &mut truncated) && !truncated);
    let mut short = vec![0; text.len()];
    assert!(!moved.to_string(&mut short, &mut truncated) && truncated);
    assert!(moved.to_string_struct(&mut dest, &mut truncated));
    assert!(!moved.to_string(&mut [], &mut truncated) && truncated);
    let mut tiny = [255; 1];
    assert!(!moved.to_string_struct(&mut tiny, &mut truncated));
    assert_eq!(tiny, [0]);
    faults::reset();
}

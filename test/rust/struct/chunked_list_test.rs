//! Stable rows survive many directory reallocations. Proves complete public
//! surface, leaf/directory OOM recovery, geometry, alignment and drop ownership.
#[path = "../fail_allocator.rs"] mod faults;
use relational_engine_scratchpad::{ChunkedList, StorageError};
use std::{cell::Cell, rc::Rc};
#[repr(align(128))]
struct Row(u64);
struct Tracked(Rc<Cell<usize>>);
impl Drop for Tracked { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }

/// Exercises stable rows across growth, allocation rollback, chunk boundaries, alignment, and destruction.
#[test]
fn stable_rows_growth_and_failure() {
    assert!(ChunkedList!(u64).unwrap().is_empty());
    assert!(ChunkedList::<u64>::zero().unwrap().is_empty());
    for rows in [0, usize::MAX, isize::MAX as usize] {
        assert!(matches!(ChunkedList::<u64>::new(rows), Err(StorageError::Layout)));
    }
    assert!(matches!(ChunkedList::<()>::new(1), Err(StorageError::Layout)));
    let mut empty = ChunkedList!(u64, 2).unwrap();
    faults::fail_after(0); // First directory admission.
    assert_eq!(empty.add(1), Err(StorageError::Allocation));
    assert_eq!(empty.len(), 0);
    faults::fail_after(1); // Directory reservation succeeds; first leaf fails.
    assert_eq!(empty.add(1), Err(StorageError::Allocation));
    assert_eq!(empty.get_chunk_count(), 0);
    assert_eq!(empty.add(1), Ok(0));
    assert_eq!(empty.add(2), Ok(1));
    faults::fail_after(0);
    assert_eq!(empty.add(3), Err(StorageError::Allocation));
    assert_eq!(empty.get_chunk_count(), 1);
    assert_eq!(empty.get_chunk(0), Some(&[1, 2][..]));
    assert_eq!(empty.add(3), Ok(2));
    let mut rows = ChunkedList!(Row, 3).unwrap();
    let mut pointers = Vec::new();
    for index in 0..1025 {
        assert_eq!(rows.add(Row(index as u64)), Ok(index));
        let pointer = rows.get(index).unwrap() as *const Row;
        assert_eq!((pointer as usize) % 128, 0);
        pointers.push(pointer);
    }
    for (index, pointer) in pointers.iter().copied().enumerate() {
        assert_eq!(rows.get(index).unwrap() as *const Row, pointer);
        assert_eq!(unsafe { (*pointer).0 }, index as u64);
    }
    assert_eq!(rows.len(), 1025);
    assert_eq!(rows.get_rows_per_chunk(), 3);
    assert_eq!(rows.get_chunk_count(), 342);
    assert_eq!(rows.get_chunk(341).unwrap().len(), 2);
    assert!(rows.get_chunk(342).is_none());
    rows.get_mut(0).unwrap().0 = 999;
    assert_eq!(unsafe { (*pointers[0]).0 }, 999);
    assert!(rows.get(1025).is_none() && rows.get_mut(usize::MAX).is_none());
    let mut dest = [0; 256];
    let mut truncated = true;
    assert!(rows.to_string(&mut dest, &mut truncated) && !truncated);
    assert!(rows.to_string_struct(&mut dest, &mut truncated));
    assert!(!rows.to_string_struct(&mut [], &mut truncated) && truncated);
    let drops = Rc::new(Cell::new(0));
    {
        let mut tracked = ChunkedList!(Tracked, 2).unwrap();
        for _ in 0..33 { tracked.add(Tracked(drops.clone())).unwrap(); }
    }
    assert_eq!(drops.get(), 33);
    faults::reset();
}

//! TypedPool owner: lazy backing, grow beyond defaults, reuse before growth,
//! empty-chunk reclamation, stable survivors, exclusive mutation and OOM rollback.
//! Handles remain owner-local; raw pointers/concurrent mutation need caller exclusion.
#[path = "../fail_allocator.rs"] mod faults;
use relational_engine_scratchpad::{TypedPool, StorageError};
use std::{cell::Cell, rc::Rc};
#[repr(align(128))]
struct Row(u64);
struct Tracked(Rc<Cell<usize>>);
impl Drop for Tracked { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }

/// Exercises lazy growth, reusable holes, empty-chunk release, stable rows, and formatting.
#[test]
fn lazy_growth_reuse_reclaim_and_stability() {
    // Even a valid huge geometry is lazy, not a reservation/touch at construction.
    faults::fail_after(0);
    let lazy = TypedPool::<u8>::new(isize::MAX as usize);
    faults::reset();
    assert!(lazy.unwrap().is_empty());
    assert_eq!(TypedPool!(u8).unwrap().get_rows_per_chunk(), 1024);
    assert!(TypedPool::<u8>::zero().unwrap().is_empty());
    assert!(matches!(TypedPool::<()>::new(1), Err(StorageError::Layout)));
    for capacity in [0, usize::MAX, isize::MAX as usize] {
        assert!(matches!(TypedPool::<u64>::new(capacity), Err(StorageError::Layout)));
    }
    let mut pool = TypedPool!(Row, 3).unwrap();
    assert_eq!(pool.get_chunk_count(), 0);
    assert!(matches!(pool.remove(0), Err(StorageError::Bounds)));
    let mut pointers = Vec::new();
    for index in 0..2050 {
        assert_eq!(pool.add(Row(index as u64)), Ok(index));
        let pointer = pool.get(index).unwrap() as *const Row;
        assert_eq!((pointer as usize) % 128, 0);
        pointers.push(pointer);
    }
    assert_eq!(pool.get_chunk_count(), 684);
    for index in 0..3 { assert_eq!(pool.remove(index).unwrap().0, index as u64); }
    assert_eq!(pool.release_empty_chunks(), 1);
    assert_eq!(pool.release_empty_chunks(), 0);
    assert_eq!(pool.get_chunk_count(), 683);
    assert!(matches!(pool.remove(0), Err(StorageError::Bounds)));
    assert!(pool.get(0).is_none() && pool.get_mut(0).is_none());
    assert!(pool.get(usize::MAX).is_none() && pool.get_mut(usize::MAX).is_none());
    assert!(matches!(pool.remove(usize::MAX), Err(StorageError::Bounds)));
    // Last allocated chunk has room: use it before recreating released chunk 0.
    assert_eq!(pool.add(Row(9000)), Ok(2050));
    assert_eq!(pool.add(Row(9001)), Ok(2051));
    assert_eq!(pool.add(Row(9002)), Ok(0));
    for (index, pointer) in pointers.iter().copied().enumerate().skip(3) {
        assert_eq!(pool.get(index).unwrap() as *const Row, pointer);
        assert_eq!(unsafe { (*pointer).0 }, index as u64);
    }
    pool.get_mut(3).unwrap().0 = 42;
    assert_eq!(unsafe { (*pointers[3]).0 }, 42);
    let mut dest = [0; 256];
    let mut truncated = true;
    assert!(pool.to_string(&mut dest, &mut truncated) && !truncated);
    assert!(pool.to_string_struct(&mut dest, &mut truncated));
    assert!(!pool.to_string(&mut [], &mut truncated) && truncated);
    assert!(!pool.to_string_struct(&mut [0; 1], &mut truncated) && truncated);
    assert_eq!(pool.get_rows_per_chunk(), 3);
    assert_eq!(pool.len(), 2050);
}

/// Verifies allocation failures preserve survivors and correctly drop rejected owned values.
#[test]
fn failure_preserves_survivors_and_incoming_ownership() {
    // Fresh pools exercise directory, rows and bitmap failures independently.
    for stage in [0, 1, 2, 3] {
        let mut pool = TypedPool!(u64, 2).unwrap();
        faults::fail_after(stage);
        let result = pool.add(1);
        faults::reset();
        assert_eq!(result, Err(StorageError::Allocation));
        assert!(pool.is_empty());
        assert_eq!(pool.get_chunk_count(), 0);
        assert_eq!(pool.add(1), Ok(0));
        assert_eq!(pool.add(2), Ok(1));
        let pointer = pool.get(0).unwrap() as *const u64;
        faults::fail_after(0);
        let failed_growth = pool.add(3);
        faults::reset();
        assert_eq!(failed_growth, Err(StorageError::Allocation));
        assert_eq!(pool.len(), 2);
        assert_eq!(pool.get_chunk_count(), 1);
        assert_eq!(pool.get(0).unwrap() as *const u64, pointer);
        assert_eq!(pool.get(1), Some(&2));
        assert_eq!(pool.add(3), Ok(2));
        pool.remove(0).unwrap();
        faults::fail_after(0);
        let reuse = pool.add(4);
        faults::reset();
        assert_eq!(reuse, Ok(0));
    }
    let drops = Rc::new(Cell::new(0));
    {
        let mut pool = TypedPool!(Tracked, 1).unwrap();
        pool.add(Tracked(drops.clone())).unwrap();
        faults::fail_after(0);
        let rejected = pool.add(Tracked(drops.clone()));
        faults::reset();
        assert_eq!(rejected, Err(StorageError::Allocation));
        assert_eq!(drops.get(), 1);
        drop(pool.remove(0).unwrap());
        assert_eq!(drops.get(), 2);
        assert_eq!(pool.release_empty_chunks(), 1);
        faults::fail_after(0);
        let rejected = pool.add(Tracked(drops.clone()));
        faults::reset();
        assert_eq!(rejected, Err(StorageError::Allocation));
        assert_eq!(drops.get(), 3);
        assert_eq!(pool.get_chunk_count(), 0);
        assert_eq!(pool.add(Tracked(drops.clone())), Ok(0));
    }
    assert_eq!(drops.get(), 4);
}

/// Compares deterministic mixed pool operations against an owned reference model.
#[test]
fn seeded_mixed_operations_match_owned_model() {
    // Reproducible xorshift sequence, not scheduler/clock-dependent fuzzing.
    let mut seed = 0x4a19_073d_94ef_71a3u64;
    println!("TypedPool model seed: {seed:#x}");
    let mut pool = TypedPool!(u64, 65).unwrap();
    let mut model: Vec<Option<u64>> = Vec::new();
    for step in 0..20_000 {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        let index = (seed as usize) % (model.len() + 2);
        match seed % 4 {
            0 => {
                let slot = pool.add(step).unwrap();
                model.resize(model.len().max(slot + 1), None);
                assert!(model[slot].is_none());
                model[slot] = Some(step);
            }
            1 => {
                let expected = model.get_mut(index).and_then(Option::take);
                assert_eq!(pool.remove(index), expected.ok_or(StorageError::Bounds));
            }
            2 => { pool.release_empty_chunks(); }
            _ => {
                if let Some(value) = pool.get_mut(index) {
                    *value = step;
                    model[index] = Some(step);
                }
            }
        }
        assert_eq!(pool.len(), model.iter().filter(|value| value.is_some()).count());
        for (index, value) in model.iter().enumerate() {
            assert_eq!(pool.get(index).copied(), *value);
        }
    }
    for (index, value) in model.iter().enumerate() {
        if value.is_some() { pool.remove(index).unwrap(); }
    }
    assert!(pool.is_empty());
    assert!(pool.release_empty_chunks() > 0);
    assert_eq!(pool.get_chunk_count(), 0);
    assert_eq!(pool.add(42), Ok(0));
}

/// Proves the pool's handle surface rejects a reused slot as stale.
#[test]
fn handles_reject_stale_generations() {
    let mut pool = TypedPool!(u64, 2).unwrap();
    let first = pool.add_handle(1).unwrap();
    let second = pool.add_handle(2).unwrap();
    assert_eq!(pool.get_handle(first), Some(&1));
    assert_eq!(pool.remove_handle(first), Ok(1));
    assert!(pool.get_handle(first).is_none());
    assert!(matches!(pool.remove_handle(first), Err(StorageError::Bounds)));
    let reused = pool.add_handle(3).unwrap(); // reuses the first slot
    assert_eq!(reused.index(), first.index());
    assert_ne!(reused.generation(), first.generation());
    assert!(pool.get_handle(first).is_none());
    assert_eq!(pool.get_handle(second), Some(&2));
    assert_eq!(pool.get_handle(reused), Some(&3));
    *pool.get_handle_mut(second).unwrap() = 22;
    assert_eq!(pool.get_handle(second), Some(&22));
}

/// Empty backing reclamation/reallocation preserves identities, including after allocation failure.
#[test]
fn released_chunks_retain_history_and_recovery() {
    use relational_engine_scratchpad::Handle;
    let mut pool = TypedPool!(u64, 1).unwrap();
    let old = pool.add_handle(10).unwrap();
    let survivor = pool.add_handle(20).unwrap();
    let address = pool.get_handle(survivor).unwrap() as *const u64;
    assert!(pool.get_handle(Handle::zero()).is_none());
    assert_eq!(pool.remove_handle(old), Ok(10));
    assert_eq!(pool.release_empty_chunks(), 1);
    assert_eq!(pool.release_empty_chunks(), 0);
    faults::fail_after(0);
    let rejected = pool.add_handle(30);
    faults::reset();
    assert_eq!(rejected, Err(StorageError::Allocation));
    assert!(pool.get_handle(old).is_none());
    let fresh = pool.add_handle(30).unwrap();
    assert_eq!(fresh.index(), old.index());
    assert_ne!(fresh.generation(), old.generation());
    assert_eq!(pool.get_handle(survivor).unwrap() as *const u64, address);
    assert!(pool.get_handle(old).is_none());
    assert_eq!(pool.remove_handle(old), Err(StorageError::Bounds));
    assert_eq!(pool.get_handle(fresh), Some(&30));
    for value in 31..200 {
        pool.remove_handle(fresh).ok(); // repeat stale rejection cannot affect later values
        let active = pool.add_handle(value).unwrap();
        assert!(pool.get_handle(old).is_none());
        pool.remove_handle(active).unwrap();
        pool.release_empty_chunks();
    }
    assert_eq!(pool.get_handle(survivor), Some(&20));
}

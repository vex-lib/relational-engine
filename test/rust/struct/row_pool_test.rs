//! RowPool owner: runtime geometry, default/explicit constructors, growth/address
//! stability, wrong-owner/zero/stale rejection, read/write, reclaim and OOM recovery.
#[path = "../fail_allocator.rs"] mod faults;
use relational_engine_scratchpad::{RowPool, RowHandle, StorageError};
use std::sync::Mutex;

static OWNER_TEST_LOCK: Mutex<()> = Mutex::new(());

#[test]
fn grow_copy_mutate_reclaim_and_owner_isolation() {
    let _guard = OWNER_TEST_LOCK.lock().unwrap();
    assert_eq!(RowPool!(3, 8).unwrap().rows_per_chunk(), 1024);
    assert!(RowPool::zero(3, 8).unwrap().is_empty());
    assert!(matches!(RowPool::new(0, 8, 1), Err(StorageError::Layout)));
    let mut pool = RowPool!(3, 256, 2).unwrap();
    let mut other = RowPool!(3, 256, 2).unwrap();
    assert_eq!((pool.row_size(), pool.alignment(), pool.rows_per_chunk()), (3, 256, 2));
    assert_ne!(pool.owner(), other.owner());
    let mut input = [1, 0, 255];
    let first = pool.add(&input).unwrap();
    input[0] = 42;
    assert_eq!(pool.get(first), Some(&[1, 0, 255][..]));
    let foreign = other.add(&input).unwrap();
    assert!(pool.get(foreign).is_none() && other.get(first).is_none());
    assert_eq!(pool.remove(foreign), Err(StorageError::Bounds));
    assert!(pool.get(RowHandle::zero()).is_none());
    let address = pool.get(first).unwrap().as_ptr();
    let mut handles = vec![first];
    for _ in 0..1025 { handles.push(pool.add(&input).unwrap()); }
    assert_eq!(pool.get(first).unwrap().as_ptr(), address);
    assert_eq!(address as usize % 256, 0);
    pool.get_mut(first).unwrap()[0] = 7;
    assert_eq!(pool.get(first), Some(&[7, 0, 255][..]));
    assert_eq!(pool.add(&[]), Err(StorageError::Layout));
    for handle in handles { pool.remove(handle).unwrap(); }
    assert!(pool.is_empty());
    assert_eq!(pool.release_empty_chunks(), 513);
    let fresh = pool.add(&input).unwrap();
    assert_eq!(fresh.index(), first.index());
    assert_ne!(fresh.generation(), first.generation());
    assert!(pool.get(first).is_none());
    assert!(pool.get(RowHandle::new(pool.owner(), usize::MAX, 1)).is_none());
    assert!(pool.get(RowHandle::new(pool.owner(), fresh.index(), 0)).is_none());
    let mut dest = [0; 300];
    let mut cut = true;
    assert!(pool.to_string(&mut dest, &mut cut) && !cut);
    assert!(pool.to_string_struct(&mut dest, &mut cut) && !cut);
    assert!(!pool.to_string(&mut [], &mut cut) && cut);
    assert!(!pool.to_string_struct(&mut [0; 1], &mut cut) && cut);
}

#[test]
fn all_fallible_growth_stages_preserve_state_and_retry() {
    let _guard = OWNER_TEST_LOCK.lock().unwrap();
    for stage in 0..5 {
        let mut pool = RowPool!(8, 8, 1).unwrap();
        faults::fail_after(stage);
        let failed = pool.add(&[1; 8]);
        faults::reset();
        assert_eq!(failed, Err(StorageError::Allocation));
        assert!(pool.is_empty());
        let live = pool.add(&[2; 8]).unwrap();
        let address = pool.get(live).unwrap().as_ptr();
        faults::fail_after(0);
        let failed = pool.add(&[3; 8]);
        faults::reset();
        assert_eq!(failed, Err(StorageError::Allocation));
        assert_eq!(pool.len(), 1);
        assert_eq!(pool.get(live), Some(&[2; 8][..]));
        assert_eq!(pool.get(live).unwrap().as_ptr(), address);
        pool.add(&[3; 8]).unwrap();
    }
}

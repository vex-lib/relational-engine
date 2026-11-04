// Owner proof for ffi/memory: rejection preserves outputs, then valid calls recover.
use relational_engine_scratchpad::ffi::memory::*;

/// Checks C ABI rejection outputs remain unchanged and valid memory calls recover afterward.
#[test]
fn ffi_rejection_preserves_outputs_and_recovers() {
    unsafe {
        relational_engine_scratchpad::ffi::re_memory_drop(std::ptr::null_mut());
        let owner = re_memory_new();
        let mut id = 99;
        assert_eq!(re_memory_copy(std::ptr::null_mut(), b"x".as_ptr(), 1, &mut id), 1);
        assert_eq!(re_memory_copy(owner, b"x".as_ptr(), 1, std::ptr::null_mut()), 1);
        assert_eq!(re_memory_copy(owner, b"x".as_ptr(), usize::MAX, &mut id), 1);
        assert_eq!(re_memory_copy(owner, std::ptr::null(), 1, &mut id), 1);
        assert_eq!(id, 99);
        assert_eq!(re_memory_copy(owner, b"hello".as_ptr(), 5, &mut id), 0);
        let mut dest = [0xaa; 5];
        let mut length = 99;
        assert_eq!(re_memory_read(std::ptr::null(), id, dest.as_mut_ptr(), 5, &mut length), 1);
        assert_eq!(re_memory_read(owner, id, dest.as_mut_ptr(), 5, std::ptr::null_mut()), 1);
        assert_eq!(re_memory_read(owner, id, dest.as_mut_ptr(), usize::MAX, &mut length), 1);
        assert_eq!(re_memory_read(owner, id, dest.as_mut_ptr(), 4, &mut length), 4);
        assert_eq!(dest, [0xaa; 5]);
        assert_eq!(length, 99);
        assert_eq!(re_memory_read(owner, 0, dest.as_mut_ptr(), 5, &mut length), 3);
        assert_eq!(re_memory_read(owner, id, std::ptr::null_mut(), 5, &mut length), 1);
        assert_eq!(re_memory_read(owner, id, dest.as_mut_ptr(), 5, &mut length), 0);
        assert_eq!(&dest, b"hello");
        assert_eq!(length, 5);
        assert_eq!(re_memory_copy(owner, std::ptr::null(), 0, &mut id), 0);
        assert_eq!(re_memory_read(owner, id, std::ptr::null_mut(), 0, &mut length), 0);
        assert_eq!(length, 0);
        re_memory_drop(owner);
    }
}

/// Uses barrier-synchronized readers and a writer to verify atomic string publication snapshots.
#[test]
fn ffi_atomic_publication_across_readers() {
    const ROUNDS: usize = 64;
    const WIDTH: usize = 32;
    const BUDGET: usize = 65536;
    unsafe {
        let owner = re_memory_new();
        let mut string_id = 0;
        assert_eq!(re_memory_new_atomic_string(owner, [0; WIDTH].as_ptr(), WIDTH, BUDGET, &mut string_id), 0);
        // Freeze topology before sharing. Calls use the same Rust-owned owner;
        // no C/Rust atomic representation is ever shared.
        let shared = &*owner;
        let barrier = std::sync::Barrier::new(3);
        std::thread::scope(|scope| {
            for _ in 0..2 {
                let barrier = &barrier;
                scope.spawn(move || {
                    for _ in 0..ROUNDS {
                        barrier.wait();
                        let mut bytes = [0; WIDTH];
                        let mut length = 0;
                        let mut truncated = true;
                        assert_eq!(re_memory_get_atomic_string(shared, string_id, bytes.as_mut_ptr(), WIDTH, &mut length, &mut truncated), 0);
                        assert_eq!(length, WIDTH);
                        assert!(!truncated && bytes.iter().all(|byte| *byte == bytes[0]));
                        barrier.wait();
                    }
                });
            }
            for round in 0..ROUNDS {
                barrier.wait();
                assert_eq!(re_memory_set_atomic_string(shared, string_id, [round as u8; WIDTH].as_ptr(), WIDTH), 0);
                barrier.wait();
            }
        });
        re_memory_drop(owner);
    }
}

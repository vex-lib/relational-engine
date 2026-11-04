use relational_engine_scratchpad::{MemoryError, primitives::atomic_string::AtomicString};
use std::sync::{Arc, Barrier};

/// Proves immutable snapshots survive replacement, budget exhaustion preserves state, and concurrent publication is coherent.
#[test]
fn snapshot_lifetime_budget_and_concurrent_publication() {
    const ROUNDS: usize = 100;
    const WIDTH: usize = 256;
    const READERS: usize = 4;
    const BUDGET: usize = 1_000_000;
    let string = AtomicString::new(b"old", BUDGET).unwrap();
    let legacy = relational_engine_scratchpad::text::atomic_string::AtomicString::new(b"compat", BUDGET).unwrap();
    assert_eq!(legacy.get(), b"compat");
    let old = string.get();
    string.set(b"new and longer\0bytes").unwrap();
    assert_eq!(old, b"old");
    assert_eq!(string.get(), b"new and longer\0bytes");
    string.set(b"").unwrap();
    assert_eq!(string.get(), b"");
    assert!(matches!(AtomicString::new(b"x", 0), Err(MemoryError::Capacity)));
    // One record header plus one byte: the exact caller-provided budget.
    let exact_budget = std::mem::size_of::<Box<[u8]>>() + 1;
    let bounded = AtomicString::new(b"x", exact_budget).unwrap();
    assert_eq!(bounded.set(b"y"), Err(MemoryError::Capacity));
    assert_eq!(bounded.get(), b"x");
    assert!(matches!(AtomicString::new(b"xx", exact_budget), Err(MemoryError::Capacity)));

    let shared = Arc::new(AtomicString::new(&[0; WIDTH], BUDGET).unwrap());
    let barrier = Arc::new(Barrier::new(READERS + 1));
    std::thread::scope(|scope| {
        for _ in 0..READERS {
            let shared = Arc::clone(&shared);
            let barrier = Arc::clone(&barrier);
            scope.spawn(move || {
                for _ in 0..ROUNDS {
                    barrier.wait();
                    let bytes = shared.get();
                    assert_eq!(bytes.len(), WIDTH);
                    assert!(bytes.iter().all(|byte| *byte == bytes[0]));
                    barrier.wait();
                }
            });
        }
        for round in 0..ROUNDS {
            barrier.wait();
            shared.set(&[round as u8; WIDTH]).unwrap();
            barrier.wait();
        }
    });
    assert_eq!(shared.get(), &[((ROUNDS - 1) as u8); WIDTH]);
}

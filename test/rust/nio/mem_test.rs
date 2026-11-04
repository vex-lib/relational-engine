// Shared owner proof for the Rust learning backend; not Vexspoke allocator parity.
use relational_engine_scratchpad::{bytes, Bytes, Memory, MemoryError};

/// Covers handle lifetime, growth, release, clear, binary bytes, and independent memory owners.
#[test]
fn memory_lifetime_growth_and_rejection() {
    let mut memory: Memory = Memory!();
    assert!(memory.is_empty());
    assert!(Memory::default().is_empty());
    assert_eq!(Bytes!("hello"), bytes!("hello"));
    assert!(relational_engine_scratchpad::memory!().is_empty());
    assert!(relational_engine_scratchpad::nio::mem::Memory::new().is_empty());
    assert!(relational_engine_scratchpad::mem::Memory::new().is_empty());
    let id = memory.copy_bytes(bytes!("hello 🌍")).unwrap();
    for _ in 0..1024 { memory.copy_bytes(b"other").unwrap(); }
    assert_eq!(memory.get(id).unwrap(), "hello 🌍".as_bytes());
    assert_eq!(memory.release(0), Err(MemoryError::UnknownHandle));
    assert_eq!(memory.len(), 1025);
    memory.release(id).unwrap();
    assert_eq!(memory.get(id), Err(MemoryError::UnknownHandle));
    assert_eq!(memory.release(id), Err(MemoryError::UnknownHandle));
    memory.clear();
    let next = memory.copy_bytes(b"").unwrap();
    assert!(next > id);
    assert_eq!(memory.get(next).unwrap(), b"");
    let binary = memory.copy_bytes(b"a\0b").unwrap();
    assert_eq!(memory.get(binary).unwrap(), b"a\0b");
    let mut independent = Memory::new();
    independent.copy_bytes(b"independent").unwrap();
    drop(memory);
    assert_eq!(independent.len(), 1);
}

/// Checks primitive kinds, atomic values, wrong-kind boundaries, and concurrent byte access.
#[test]
fn primitive_kind_boundaries_and_atomic_cells() {
    const SNAPSHOT_BUDGET: usize = 4096;
    let mut memory = Memory!();
    let bytes = memory.copy_bytes(&[0, 255]).unwrap();
    assert_eq!(memory.get_byte(bytes, 0), Ok(0));
    assert_eq!(memory.get_byte(bytes, 1), Ok(255));
    assert_eq!(memory.get_byte(bytes, 2), Err(MemoryError::Bounds));
    assert_eq!(memory.get_byte(bytes, usize::MAX), Err(MemoryError::Bounds));
    let byte = memory.new_atomic_byte(0).unwrap();
    assert_eq!(memory.get_atomic_byte(byte), Ok(0));
    memory.set_atomic_byte(byte, 255).unwrap();
    assert_eq!(memory.get_atomic_byte(byte), Ok(255));
    assert_eq!(memory.get(byte), Err(MemoryError::WrongKind));
    assert_eq!(memory.get_atomic_byte(bytes), Err(MemoryError::WrongKind));
    assert_eq!(memory.set_atomic_byte(bytes, 1), Err(MemoryError::WrongKind));
    let string = memory.new_atomic_string(b"old", SNAPSHOT_BUDGET).unwrap();
    let old = memory.get_atomic_string(string).unwrap();
    memory.set_atomic_string(string, b"new longer string").unwrap();
    assert_eq!(old, b"old");
    assert_eq!(memory.get_atomic_string(string).unwrap(), b"new longer string");
    assert_eq!(memory.get_atomic_string(byte), Err(MemoryError::WrongKind));
    assert_eq!(memory.set_atomic_string(byte, b"wrong"), Err(MemoryError::WrongKind));
    let count = memory.len();
    assert_eq!(memory.new_atomic_string(b"x", 0), Err(MemoryError::Capacity));
    assert_eq!(memory.len(), count);
    let barrier = std::sync::Barrier::new(4);
    std::thread::scope(|scope| {
        for value in [0, 1, 254, 255] {
            let memory = &memory;
            let barrier = &barrier;
            scope.spawn(move || {
                barrier.wait();
                for _ in 0..1000 {
                    memory.set_atomic_byte(byte, value).unwrap();
                    assert!([0, 1, 254, 255].contains(&memory.get_atomic_byte(byte).unwrap()));
                }
            });
        }
    });
    memory.release(byte).unwrap();
    assert_eq!(memory.get_atomic_byte(byte), Err(MemoryError::UnknownHandle));
    assert_eq!(memory.set_atomic_byte(byte, 1), Err(MemoryError::UnknownHandle));
    memory.clear();
    assert_eq!(memory.get_atomic_string(string), Err(MemoryError::UnknownHandle));
}

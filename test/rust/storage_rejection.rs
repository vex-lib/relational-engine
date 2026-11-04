//! Compile-negative fixture: each cfg selects a single intended error.
use relational_engine_scratchpad::*;
fn main() {
    #[cfg(chunk_arity)] let _ = Chunk!(u64, 2, 3);
    #[cfg(list_arity)] let _ = ChunkedList!(u64, 2, 3);
    #[cfg(slot_arity)] let _ = VariableSlot!(b"x", std::ptr::null(), 3);
    #[cfg(registry_arity)] let _ = VariableRegistry!(2, 3);
    #[cfg(wrong_capacity)] let _ = Chunk!(u64, "wrong");
    #[cfg(typed_chunk_arity)] let _ = TypedChunk!(u64, 2, 3);
    #[cfg(typed_pool_arity)] let _ = TypedPool!(u64, 2, 3);
    #[cfg(typed_wrong_capacity)] let _ = TypedPool!(u64, "wrong");
    #[cfg(row_pool_arity)] let _ = RowPool!(8, 8, 2, 3);
    #[cfg(row_chunk_arity)] let _ = RowChunk!(8, 8);
    #[cfg(row_handle_arity)] let _ = RowHandle!(1, 2);
    #[cfg(row_wrong_capacity)] let _ = RowPool!(8, 8, "wrong");
    #[cfg(row_pool_borrow)] {
        let mut owner = RowPool!(8, 8, 2).unwrap();
        let handle = owner.add(&[0; 8]).unwrap();
        let borrowed = owner.get(handle).unwrap();
        owner.remove(handle).unwrap();
        println!("{borrowed:?}");
    }
    #[cfg(row_pool_release_borrow)] {
        let mut owner = RowPool!(8, 8, 2).unwrap();
        let handle = owner.add(&[0; 8]).unwrap();
        let borrowed = owner.get(handle).unwrap();
        owner.release_empty_chunks();
        println!("{borrowed:?}");
    }
    #[cfg(typed_chunk_borrow)] {
        let mut owner = TypedChunk!(u64, 2).unwrap();
        owner.add(1).unwrap();
        let borrowed = owner.get(0).unwrap();
        owner.remove(0).unwrap();
        println!("{borrowed}");
    }
    #[cfg(typed_pool_borrow)] {
        let mut owner = TypedPool!(u64, 2).unwrap();
        owner.add(1).unwrap();
        let borrowed = owner.get(0).unwrap();
        owner.remove(0).unwrap();
        println!("{borrowed}");
    }
    #[cfg(typed_pool_release_borrow)] {
        let mut owner = TypedPool!(u64, 2).unwrap();
        owner.add(1).unwrap();
        let borrowed = owner.get(0).unwrap();
        owner.release_empty_chunks();
        println!("{borrowed}");
    }
    #[cfg(chunk_borrow)] {
        let mut owner = Chunk!(u64, 2).unwrap();
        owner.add(1).unwrap();
        let borrowed = owner.get(0).unwrap();
        owner.add(2).unwrap();
        println!("{borrowed}");
    }
    #[cfg(registry_borrow)] {
        let mut owner = VariableRegistry!().unwrap();
        owner.add(b"a", std::ptr::null()).unwrap();
        let borrowed = owner.get(0).unwrap();
        owner.add(b"b", std::ptr::null()).unwrap();
        println!("{:?}", borrowed.get_name());
    }
}

//! Compiled by the registered runner for intended unsafe/arity/type/borrow failures.
use relational_engine_scratchpad::{FixedMappedFile, PreallocatedFile};

fn main() {
    #[cfg(unsafe_admission)]
    let _owner = FixedMappedFile!("fixture");
    #[cfg(unsafe_explicit)]
    let _owner = FixedMappedFile::open("fixture", true);
    #[cfg(arity)]
    let _owner = FixedMappedFile!("fixture", true, 1);
    #[cfg(wrong_mode)]
    let _owner = unsafe { FixedMappedFile!("fixture", 1) };
    #[cfg(wrong_path)]
    let _owner = unsafe { FixedMappedFile!(12u32) };
    #[cfg(close_borrow)]
    {
        let mut owner = FixedMappedFile!();
        let bytes = owner.as_slice();
        owner.close();
        println!("{bytes:?}");
    }
    #[cfg(write_borrow)]
    {
        let mut owner = FixedMappedFile!();
        let bytes = owner.as_slice();
        let _ = owner.write(0, &[1]);
        println!("{bytes:?}");
    }
    #[cfg(drop_borrow)]
    {
        let owner = FixedMappedFile!();
        let bytes = owner.as_slice();
        drop(owner);
        println!("{bytes:?}");
    }
    #[cfg(double_mut)]
    {
        let mut owner = FixedMappedFile!();
        let first = owner.as_mut_slice();
        let second = owner.as_mut_slice();
        println!("{first:?} {second:?}");
    }
    #[cfg(preallocation_arity)]
    let _owner = PreallocatedFile!("fixture");
    #[cfg(preallocation_extra_arity)]
    let _owner = PreallocatedFile!("fixture", 8, true);
    #[cfg(preallocation_length)]
    let _owner = PreallocatedFile!("fixture", -1i64);
    #[cfg(preallocation_move)]
    {
        let owner = PreallocatedFile!();
        let _ = unsafe { FixedMappedFile::from_preallocated(owner) };
        println!("{}", owner.len());
    }
    #[cfg(preallocation_borrow)]
    {
        let mut owner = PreallocatedFile!();
        let path = owner.path();
        owner.close();
        println!("{path:?}");
    }
    #[cfg(preallocation_unsafe)]
    let _ = FixedMappedFile::from_preallocated(PreallocatedFile!());
}

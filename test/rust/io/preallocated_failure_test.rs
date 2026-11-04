//! Compile the EXACT production owner against a test-only backend seam to attack
//! every reservation/EOF/query stage and cleanup reporting deterministically.
//! No production test hooks or fixtures are shipped; real allocation proof belongs
//! to preallocated_file_test/preallocation_test. Stable parent is a caller contract.
mod annotation { pub use relational_engine_scratchpad::annotation::*; }
mod preallocation_error { pub use relational_engine_scratchpad::PreallocationError; }
#[path = "../../../../host-behavior/relational-engine/rust/src/nio/projection.rs"]
pub(crate) mod projection;
mod nio { pub(crate) use crate::projection; }
#[path = "../../../../host-behavior/relational-engine/rust/src/io/preallocated_file.rs"]
mod subject;
use std::{cell::Cell, fs, path::PathBuf};

thread_local! { static STAGE: Cell<u8> = const { Cell::new(0) }; }

mod preallocation {
    use super::{STAGE, preallocation_error::PreallocationError};
    use std::{fs::File, io};
    pub(crate) fn supported() -> bool { STAGE.with(|stage| stage.get() != 5) }
    pub(crate) fn reserve(_file: &File, _length: u64) -> Result<(), PreallocationError> {
        if STAGE.with(|stage| stage.get()) == 1 { return Err(io::Error::from_raw_os_error(28).into()); }
        Ok(())
    }
    pub(crate) fn set_length(file: &File, length: u64) -> Result<(), PreallocationError> {
        if STAGE.with(|stage| stage.get()) == 2 { return Err(io::Error::new(io::ErrorKind::PermissionDenied, "EOF failure").into()); }
        file.set_len(length)?;
        Ok(())
    }
    pub(crate) fn allocated_bytes(_file: &File) -> Result<u64, PreallocationError> {
        match STAGE.with(|stage| stage.get()) {
            3 => Err(io::Error::new(io::ErrorKind::Other, "query failure").into()),
            4 => Ok(7),
            _ => Ok(8),
        }
    }
}

#[test]
fn failure_stages_preserve_existing_and_allow_retry() {
    use preallocation_error::PreallocationError;
    let directory: PathBuf = std::env::temp_dir().join(format!("re-prealloc-faults-{}", std::process::id()));
    fs::create_dir(&directory).unwrap();
    let path = directory.join("storage");
    for stage in 1..=5 {
        STAGE.with(|value| value.set(stage));
        let rejected = subject::PreallocatedFile::new(&path, 8);
        match stage {
            1 => assert!(matches!(rejected, Err(PreallocationError::Io(ref cause)) if cause.raw_os_error() == Some(28))),
            2 | 3 => assert!(matches!(rejected, Err(PreallocationError::Io(_)))),
            4 => assert!(matches!(rejected, Err(PreallocationError::Incomplete { requested: 8, allocated: 7 }))),
            5 => assert!(matches!(rejected, Err(PreallocationError::Unsupported))),
            _ => unreachable!(),
        }
        assert!(!path.exists());
        STAGE.with(|value| value.set(0));
        let mut retry = subject::PreallocatedFile::new(&path, 8).unwrap();
        assert_eq!(retry.len(), 8);
        assert!(retry.is_open() && !retry.is_empty());
        assert_eq!(retry.path(), Some(path.as_path()));
        assert_eq!(retry.allocated_bytes().unwrap(), 8);
        retry.sync().unwrap();
        let mut dest = [0; 300];
        let mut cut = false;
        assert!(retry.to_string(&mut dest, &mut cut));
        assert!(retry.to_string_struct(&mut dest, &mut cut));
        retry.close();
        fs::remove_file(&path).unwrap();
    }
    assert!(subject::PreallocatedFile::zero().into_file().is_err());
    fs::remove_dir(directory).unwrap();
}

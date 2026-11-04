//! PreallocatedFile owner: real physical reservation (including decimal 1 GB),
//! create-new preservation, invalid sizes, private permissions, cleanup/retry,
//! getters/projections and descriptor transfer into writable mapped offset access.
//! All files live in disposable scratch directories, NOT actual Application Support.
//! This is macOS/Linux reservation proof, not Windows, transactions, future COW
//! space guarantees or a hostile writable-parent sandbox. Runner provides watchdog.
use relational_engine_scratchpad::{PreallocatedFile, PreallocationError, FixedMappedFile, MappingError};
use std::{fs, path::PathBuf, sync::atomic::{AtomicUsize, Ordering}};

const ONE_GIGABYTE: u64 = 1_000_000_000;
const SMALL_EXTENT: u64 = 65_537;
// This exceeds this lab filesystem's capacity without filling it: allocate-all
// must reject the request instead of creating a giant sparse file.
const IMPOSSIBLE_LAB_EXTENT: u64 = 1 << 60;
static NEXT: AtomicUsize = AtomicUsize::new(0);

struct Fixture { dir: PathBuf, path: PathBuf }
impl Fixture {
    /// Create only a private scratch directory; successful files are caller-owned.
    fn new() -> Self {
        let dir = std::env::temp_dir().join(format!("re-prealloc-{}-{}", std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)));
        fs::create_dir(&dir).unwrap();
        let path = dir.join("storage.bin");
        Self { dir, path }
    }
}
impl Drop for Fixture {
    /// Remove just this isolated fixture after mappings/file owners drop.
    fn drop(&mut self) { fs::remove_dir_all(&self.dir).unwrap(); }
}

#[test]
fn one_gigabyte_is_physically_reserved_and_bits_survive_reopen() {
    let fixture = Fixture::new();
    let support = fixture.dir.join("Library/Application Support/vexgraph/lab");
    fs::create_dir_all(&support).unwrap();
    let path = support.join("storage.bin");
    let owner = PreallocatedFile!(&path, ONE_GIGABYTE).unwrap();
    assert_eq!(owner.len(), ONE_GIGABYTE);
    assert!(owner.is_open() && !owner.is_empty());
    assert!(owner.allocated_bytes().unwrap() >= ONE_GIGABYTE);
    assert_eq!(fs::metadata(&path).unwrap().len(), ONE_GIGABYTE);
    owner.sync().unwrap();
    // SAFETY: private stable file, no other content access until mapping closes.
    let mut mapping = unsafe { FixedMappedFile::from_preallocated(owner).unwrap() };
    assert_eq!(mapping.len(), ONE_GIGABYTE as usize);
    assert!(mapping.is_writable());
    for offset in [0, ONE_GIGABYTE as usize / 2, ONE_GIGABYTE as usize - 1] {
        assert_eq!(mapping.read(offset, 1).unwrap(), &[0]);
        mapping.as_mut_slice().unwrap()[offset] ^= 0b1010_0101;
        assert_eq!(mapping.read(offset, 1).unwrap(), &[0b1010_0101]);
    }
    assert!(matches!(mapping.write(ONE_GIGABYTE as usize, &[1]), Err(MappingError::Bounds)));
    mapping.sync().unwrap();
    mapping.close();
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        assert!(fs::metadata(&path).unwrap().blocks() * 512 >= ONE_GIGABYTE);
    }
    // SAFETY: previous mapping closed; file remains exclusively owned by fixture.
    let reopened = unsafe { FixedMappedFile::new(&path).unwrap() };
    for offset in [0, ONE_GIGABYTE as usize / 2, ONE_GIGABYTE as usize - 1] {
        assert_eq!(reopened.read(offset, 1).unwrap(), &[0b1010_0101]);
    }
    println!("PASS: decimal 1 GB logical extent and >=1 GB allocated OS blocks, offset bit flips persist");
}

#[test]
fn invalid_sizes_existing_paths_permissions_and_recovery() {
    let fixture = Fixture::new();
    for length in [0, u64::MAX, isize::MAX as u64 + 1] {
        assert!(matches!(PreallocatedFile::new(&fixture.path, length), Err(PreallocationError::Length)));
        assert!(!fixture.path.exists());
    }
    assert!(matches!(PreallocatedFile::new(fixture.dir.join("missing/bytes"), 1), Err(PreallocationError::Io(_))));
    assert!(matches!(PreallocatedFile::new("", 1), Err(PreallocationError::Io(_))));
    fs::write(&fixture.path, b"keep existing bytes").unwrap();
    for _ in 0..3 {
        assert!(matches!(PreallocatedFile!(&fixture.path, SMALL_EXTENT),
            Err(PreallocationError::Io(ref error)) if error.kind() == std::io::ErrorKind::AlreadyExists));
        assert_eq!(fs::read(&fixture.path).unwrap(), b"keep existing bytes");
    }
    #[cfg(unix)]
    {
        let alias = fixture.dir.join("alias");
        std::os::unix::fs::symlink(&fixture.path, &alias).unwrap();
        assert!(matches!(PreallocatedFile::new(&alias, 1), Err(PreallocationError::Io(_))));
        assert_eq!(fs::read(&fixture.path).unwrap(), b"keep existing bytes");
    }
    fs::remove_file(&fixture.path).unwrap();
    let owner = PreallocatedFile::new(&fixture.path, SMALL_EXTENT).unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        assert_eq!(fs::metadata(&fixture.path).unwrap().permissions().mode() & 0o077, 0);
    }
    drop(owner);
    assert_eq!(fs::metadata(&fixture.path).unwrap().len(), SMALL_EXTENT);
}

#[test]
fn reservation_failure_removes_new_artifact_and_retry_works() {
    let fixture = Fixture::new();
    assert!(matches!(PreallocatedFile::new(&fixture.path, IMPOSSIBLE_LAB_EXTENT), Err(PreallocationError::Io(_))));
    assert!(!fixture.path.exists());
    let owner = PreallocatedFile::new(&fixture.path, 1).unwrap();
    assert_eq!(owner.len(), 1);
    assert!(owner.allocated_bytes().unwrap() >= 1);
}

#[test]
fn closed_identity_getters_and_bounded_escaped_projections() {
    for mut closed in [PreallocatedFile::zero(), PreallocatedFile!()] {
        assert_eq!(closed.len(), 0);
        assert!(closed.is_empty() && !closed.is_open());
        assert_eq!(closed.path(), None);
        assert!(matches!(closed.allocated_bytes(), Err(PreallocationError::Closed)));
        assert!(matches!(closed.sync(), Err(PreallocationError::Closed)));
        closed.close();
        closed.close();
        // SAFETY: closed owner is rejected before mapping admission.
        assert!(matches!(unsafe { FixedMappedFile::from_preallocated(closed) },
            Err(MappingError::Preallocation(PreallocationError::Closed))));
    }
    let fixture = Fixture::new();
    let path = fixture.dir.join("quoted\"newline\nfile");
    let mut owner = PreallocatedFile!(&path, 17).unwrap();
    assert_eq!(owner.path(), Some(path.as_path()));
    let mut dest = [0; 600];
    let mut cut = true;
    assert!(owner.to_string(&mut dest, &mut cut) && !cut);
    let length = dest.iter().position(|byte| *byte == 0).unwrap();
    assert_eq!(std::str::from_utf8(&dest[..length]).unwrap(), "PreallocatedFile(open=true, len=17)");
    let mut exact = vec![0; length + 1];
    assert!(owner.to_string(&mut exact, &mut cut) && !cut);
    exact.pop();
    assert!(!owner.to_string(&mut exact, &mut cut) && cut);
    assert_eq!(exact.last(), Some(&0));
    assert!(owner.to_string_struct(&mut dest, &mut cut) && !cut);
    let text = std::str::from_utf8(&dest[..dest.iter().position(|byte| *byte == 0).unwrap()]).unwrap();
    assert!(text.contains("\\\"") && text.contains("\\n"));
    assert!(text.find("file:").unwrap() < text.find("path:").unwrap());
    assert!(text.find("path:").unwrap() < text.find("length:").unwrap());
    assert!(!owner.to_string(&mut [], &mut cut) && cut);
    assert!(!owner.to_string_struct(&mut [1], &mut cut) && cut);
    owner.close();
    assert!(path.exists()); // close never deletes the successful file
}

#[cfg(unix)]
#[test]
fn mapping_transfer_uses_original_descriptor_not_replaced_path() {
    let fixture = Fixture::new();
    let owner = PreallocatedFile::new(&fixture.path, 31).unwrap();
    fs::remove_file(&fixture.path).unwrap();
    fs::write(&fixture.path, b"replacement inode").unwrap();
    // SAFETY: original unlinked inode is owned exclusively; pathname replacement
    // did not mutate/truncate the original retained file.
    let mut mapping = unsafe { FixedMappedFile::from_preallocated(owner).unwrap() };
    assert_eq!(mapping.len(), 31);
    assert_eq!(mapping.read(0, 1).unwrap(), &[0]);
    mapping.write(0, &[9]).unwrap();
    mapping.close();
    assert_eq!(fs::read(&fixture.path).unwrap(), b"replacement inode");
}

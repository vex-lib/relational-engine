//! FixedMappedFile owner: every public form, fixed byte extents, exclusive writes,
//! rejection preservation, explicit writeback/sync, close/drop and shared readers.
//! Files are private disposable fixtures; no external mutation occurs while mapped.
//! External hostile truncation is outside unsafe admission, not a safe-rejection
//! claim. Resize/C ABI/database/crash durability are not offered. Real flush/sync
//! mapping/flush/sync failure injection and Rust sanitizer/Windows/macOS14 runs
//! remain stated gaps. Length representability is guarded but huge-file admission
//! is not forced through host filesystem limits in this suite.
use relational_engine_scratchpad::{FixedMappedFile, MappingError};
use std::{fs, path::PathBuf, sync::{atomic::{AtomicUsize, Ordering}, Barrier}};

static NEXT: AtomicUsize = AtomicUsize::new(0);

struct Fixture { dir: PathBuf, path: PathBuf }

impl Fixture {
    /// Create a unique private directory with an exact byte file; never touch user files.
    fn new(bytes: &[u8]) -> Self {
        let dir = std::env::temp_dir().join(format!("re-mapped-{}-{}",
            std::process::id(), NEXT.fetch_add(1, Ordering::Relaxed)));
        fs::create_dir(&dir).unwrap();
        let path = dir.join("bytes.bin");
        fs::write(&path, bytes).unwrap();
        Self { dir, path }
    }
}

impl Drop for Fixture {
    /// Remove only the test's private directory, after all mappings have been released.
    fn drop(&mut self) { fs::remove_dir_all(&self.dir).unwrap(); }
}

#[test]
fn constructor_forms_read_bounds_and_closed_defaults() {
    let fixture = Fixture::new(&[0, 1, 255]);
    for mut closed in [FixedMappedFile::zero(), FixedMappedFile!()] {
        assert!(!closed.is_open() && !closed.is_writable() && closed.is_empty());
        assert_eq!(closed.len(), 0);
        assert_eq!(closed.as_slice(), &[]);
        assert!(matches!(closed.as_mut_slice(), Err(MappingError::Closed)));
        assert!(matches!(closed.read(0, 0), Err(MappingError::Closed)));
        assert!(matches!(closed.write(0, &[]), Err(MappingError::Closed)));
        assert!(matches!(closed.flush(), Err(MappingError::Closed)));
        assert!(matches!(closed.sync(), Err(MappingError::Closed)));
        closed.close();
        closed.close();
    }
    // SAFETY: fixture has no external file users and lives longer than all mappings.
    let owners = unsafe { [FixedMappedFile::new(&fixture.path).unwrap(),
        FixedMappedFile!(&fixture.path).unwrap(), FixedMappedFile!(&fixture.path, false).unwrap(),
        FixedMappedFile::open(&fixture.path, false).unwrap()] };
    for mut owner in owners {
        assert!(owner.is_open() && !owner.is_empty() && !owner.is_writable());
        assert_eq!(owner.len(), 3);
        assert_eq!(owner.as_slice(), &[0, 1, 255]);
        assert_eq!(owner.read(1, 1).unwrap(), &[1]);
        assert_eq!(owner.read(3, 0).unwrap(), &[]);
        for (offset, length) in [(0, 4), (4, 0), (usize::MAX, 1), (1, usize::MAX)] {
            assert!(matches!(owner.read(offset, length), Err(MappingError::Bounds)));
        }
        assert!(matches!(owner.as_mut_slice(), Err(MappingError::ReadOnly)));
        assert!(matches!(owner.write(0, &[9]), Err(MappingError::ReadOnly)));
        assert!(matches!(owner.flush(), Err(MappingError::ReadOnly)));
        assert!(matches!(owner.sync(), Err(MappingError::ReadOnly)));
        assert_eq!(owner.as_slice(), &[0, 1, 255]);
        owner.close();
        assert!(!owner.is_open() && owner.is_empty());
    }
}

#[test]
fn writable_roundtrip_failure_preservation_and_drop() {
    let fixture = Fixture::new(&vec![0; 131_073]);
    // SAFETY: this is the only view; no file IO occurs until close/drop.
    let mut owner = unsafe { FixedMappedFile!(&fixture.path, true).unwrap() };
    assert!(owner.is_writable());
    assert_eq!(owner.len(), 131_073);
    owner.write(0, &[0, 255, 7]).unwrap();
    owner.write(65_535, &[11, 12, 13]).unwrap();
    owner.write(owner.len() - 1, &[42]).unwrap();
    owner.write(owner.len(), &[]).unwrap();
    owner.as_mut_slice().unwrap()[1] = 23;
    let before = owner.as_slice().to_vec();
    for (offset, source) in [(owner.len(), &[9][..]), (owner.len() + 1, &[][..]),
        (usize::MAX, &[1, 2][..])] {
        assert!(matches!(owner.write(offset, source), Err(MappingError::Bounds)));
        assert_eq!(owner.as_slice(), before);
    }
    owner.write(3, &[9]).unwrap(); // valid recovery after rejection
    owner.flush().unwrap();
    owner.sync().unwrap();
    let expected = owner.as_slice().to_vec();
    owner.close();
    assert_eq!(fs::read(&fixture.path).unwrap(), expected);
    // SAFETY: prior mapping is closed; no external access until drop.
    let reopened = unsafe { FixedMappedFile::new(&fixture.path).unwrap() };
    assert_eq!(reopened.as_slice(), expected);
    drop(reopened); // natural field drop unmaps before closing the descriptor
    fs::remove_file(&fixture.path).unwrap();
    fs::write(&fixture.path, b"recreated").unwrap();
    // SAFETY: fresh private file with no competing access.
    let recreated = unsafe { FixedMappedFile::new(&fixture.path).unwrap() };
    assert_eq!(recreated.as_slice(), b"recreated");
}

#[test]
fn empty_file_admission_missing_and_nonregular_recovery() {
    let fixture = Fixture::new(&[]);
    // SAFETY: empty private fixture, no file mutation while any owner lives.
    let mut readonly = unsafe { FixedMappedFile::new(&fixture.path).unwrap() };
    assert!(readonly.is_open() && readonly.is_empty());
    assert_eq!(readonly.read(0, 0).unwrap(), &[]);
    assert!(matches!(readonly.read(0, 1), Err(MappingError::Bounds)));
    assert!(matches!(readonly.as_mut_slice(), Err(MappingError::ReadOnly)));
    readonly.close();
    // SAFETY: prior owner closed and fixture is exclusively held.
    let mut writable = unsafe { FixedMappedFile::open(&fixture.path, true).unwrap() };
    assert!(writable.is_open() && writable.is_empty() && writable.is_writable());
    assert!(writable.as_mut_slice().unwrap().is_empty());
    writable.write(0, &[]).unwrap();
    assert!(matches!(writable.write(0, &[1]), Err(MappingError::Bounds)));
    writable.flush().unwrap();
    writable.sync().unwrap();
    writable.close();
    // SAFETY: these admissions either reject or concern private stable files.
    unsafe {
        for _ in 0..3 {
            let missing = FixedMappedFile::new(fixture.dir.join("absent"));
            assert!(matches!(missing, Err(MappingError::Io(ref error))
                if error.kind() == std::io::ErrorKind::NotFound));
            assert!(matches!(FixedMappedFile::new(""), Err(MappingError::Io(_))));
        }
        #[cfg(unix)]
        assert!(matches!(FixedMappedFile::new(&fixture.dir), Err(MappingError::NotRegularFile)));
    }
    fs::write(&fixture.path, b"retry").unwrap();
    // SAFETY: no earlier mapping survived admission/close.
    let retry = unsafe { FixedMappedFile::new(&fixture.path).unwrap() };
    assert_eq!(retry.as_slice(), b"retry");
}

#[test]
fn projections_and_synchronized_readers() {
    let fixture = Fixture::new(b"shared read-only bytes");
    // SAFETY: fixture is immutable until owner drop; readers share immutable borrows.
    let owner = unsafe { FixedMappedFile::new(&fixture.path).unwrap() };
    let start = Barrier::new(4);
    std::thread::scope(|scope| {
        for _ in 0..3 {
            let owner = &owner;
            let start = &start;
            scope.spawn(move || {
                start.wait();
                for _ in 0..1000 { assert_eq!(owner.read(0, owner.len()).unwrap(), b"shared read-only bytes"); }
            });
        }
        start.wait();
    });
    let mut buffer = [0; 300];
    let mut truncated = true;
    assert!(owner.to_string(&mut buffer, &mut truncated) && !truncated);
    let text = std::str::from_utf8(&buffer[..buffer.iter().position(|b| *b == 0).unwrap()]).unwrap();
    assert_eq!(text, "FixedMappedFile(open=true, len=22, writable=false)");
    let mut exact = vec![0; text.len() + 1];
    assert!(owner.to_string(&mut exact, &mut truncated) && !truncated);
    exact.pop();
    assert!(!owner.to_string(&mut exact, &mut truncated) && truncated);
    assert_eq!(exact.last(), Some(&0));
    assert!(owner.to_string_struct(&mut buffer, &mut truncated) && !truncated);
    let text = std::str::from_utf8(&buffer[..buffer.iter().position(|b| *b == 0).unwrap()]).unwrap();
    assert_eq!(text, "FixedMappedFile { read: true, write: false, file: true, writable: false }");
    assert!(!owner.to_string(&mut [], &mut truncated) && truncated);
    assert!(!owner.to_string_struct(&mut [1], &mut truncated) && truncated);
    assert!(FixedMappedFile!().to_string(&mut buffer, &mut truncated) && !truncated);
}

#[test]
fn one_byte_extent_and_exact_write_capacity() {
    let fixture = Fixture::new(&[255]);
    // SAFETY: private single-owner file, unchanged externally until close/drop.
    let mut owner = unsafe { FixedMappedFile::open(&fixture.path, true).unwrap() };
    assert_eq!(owner.len(), 1);
    assert_eq!(owner.read(0, 1).unwrap(), &[255]);
    owner.write(0, &[0]).unwrap();
    assert!(matches!(owner.write(0, &[1, 2]), Err(MappingError::Bounds)));
    assert_eq!(owner.as_slice(), &[0]);
    owner.sync().unwrap();
    owner.close();
    assert_eq!(fs::read(&fixture.path).unwrap(), &[0]);
}

#[cfg(unix)]
#[test]
fn retained_file_survives_path_replacement_and_symlink_follows_os() {
    let fixture = Fixture::new(b"original inode");
    let alias = fixture.dir.join("alias");
    std::os::unix::fs::symlink(&fixture.path, &alias).unwrap();
    // SAFETY: only the directory entry changes below, never the mapped inode's
    // content/extent. Both original bytes and their immutable mapping remain live.
    let owner = unsafe { FixedMappedFile::new(&alias).unwrap() };
    fs::remove_file(&fixture.path).unwrap();
    fs::write(&fixture.path, b"replacement").unwrap();
    assert_eq!(owner.as_slice(), b"original inode");
    drop(owner);
    // SAFETY: old view gone; new inode has no external mutations while mapped.
    let reopened = unsafe { FixedMappedFile::new(&alias).unwrap() };
    assert_eq!(reopened.as_slice(), b"replacement");
}

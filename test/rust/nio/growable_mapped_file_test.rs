//! GrowableMappedFile owner: the fixed reservation, sparse step growth, allocate
//! before publish, reopening an existing file without resizing it, and above all
//! that growth NEVER remaps — a pointer taken before a growth step is still the same
//! pointer after it. Files are private disposable fixtures; no external mutation
//! occurs while mapped. Hostile truncation, shrink, the C ABI, a database format and
//! crash durability are not offered here.
use relational_engine_scratchpad::{GrowableMappedFile, MappingError};
use std::{fs, path::PathBuf, sync::atomic::{AtomicUsize, Ordering}};

static NEXT: AtomicUsize = AtomicUsize::new(0);

struct Fixture { dir: PathBuf, path: PathBuf }

impl Fixture {
    /// Unique private directory; never touches user files.
    fn new() -> Self {
        let dir = std::env::temp_dir().join(format!("re-growable-{}-{}",
            std::process::id(), NEXT.fetch_add(1, Ordering::Relaxed)));
        fs::create_dir(&dir).unwrap();
        let path = dir.join("grow.bin");
        Self { dir, path }
    }
}

impl Drop for Fixture {
    fn drop(&mut self) { let _ = fs::remove_dir_all(&self.dir); }
}

#[test]
fn create_grow_and_never_move_the_mapping() {
    if !GrowableMappedFile::supported() {
        return; // no reservation backend on this host: create rejects, proven below
    }
    let fixture = Fixture::new();
    // SAFETY: private fixture directory; nothing else opens or truncates it.
    let mut file = unsafe { GrowableMappedFile::create(&fixture.path, 4096, 512) }.unwrap();
    assert_eq!(file.len(), 512);
    assert_eq!(file.reserved(), 4096);
    assert!(file.is_open() && !file.is_empty());
    assert!(file.allocated_bytes().unwrap() >= 512);

    let payload: Vec<u8> = (0..64u8).collect();
    file.write(0, &payload).unwrap();
    assert_eq!(file.read(0, 64).unwrap(), &payload[..]);

    // Past the committed length is refused — never exposed, reservation or not.
    assert!(matches!(file.write(512, &[1]), Err(MappingError::Bounds)));
    assert!(matches!(file.read(511, 2), Err(MappingError::Bounds)));
    assert!(matches!(file.read(usize::MAX, 2), Err(MappingError::Bounds)));

    // THE INVARIANT this type exists for: growth allocates and publishes, and the
    // mapping is never remade, so pointers taken before the step stay valid.
    let before = file.as_slice().as_ptr();
    file.grow_to(2048).unwrap();
    assert_eq!(file.len(), 2048);
    assert!(file.allocated_bytes().unwrap() >= 2048);
    assert_eq!(file.as_slice().as_ptr(), before, "growth must not move the mapping");
    assert_eq!(file.read(0, 64).unwrap(), &payload[..], "bytes survive growth");

    file.write(2000, &[7, 8, 9]).unwrap();
    assert_eq!(file.read(2000, 3).unwrap(), &[7, 8, 9]);

    // Bounded by the reservation, and never backwards.
    assert!(file.grow_to(4096).is_ok());
    assert!(matches!(file.grow_to(4097), Err(MappingError::Bounds)));
    assert!(matches!(file.grow_to(100), Err(MappingError::Bounds)));
    assert!(file.grow_to(4096).is_ok(), "the same length is a no-op");

    file.flush().unwrap();
    file.sync().unwrap();
    file.close();
    assert!(!file.is_open() && file.len() == 0 && file.as_slice().is_empty());
    assert!(matches!(file.read(0, 1), Err(MappingError::Closed)));
    assert!(matches!(file.as_mut_slice(), Err(MappingError::Closed)));
    assert!(matches!(file.grow_to(1), Err(MappingError::Closed)));
    assert!(matches!(file.flush(), Err(MappingError::Closed)));
    assert!(matches!(file.allocated_bytes(), Err(MappingError::Closed)));
    file.close();
}

#[test]
fn admission_empty_owner_and_projections() {
    let zero = GrowableMappedFile::zero();
    assert!(!zero.is_open() && zero.len() == 0 && zero.reserved() == 0);
    assert!(zero.as_slice().is_empty());
    assert!(matches!(zero.read(0, 0), Err(MappingError::Closed)));

    // An existing destination is never clobbered.
    let occupied = Fixture::new();
    fs::write(&occupied.path, b"occupied").unwrap();
    // SAFETY: private fixture directory.
    assert!(unsafe { GrowableMappedFile::create(&occupied.path, 4096, 0) }.is_err());
    assert_eq!(fs::read(&occupied.path).unwrap(), b"occupied");

    let fixture = Fixture::new();
    // committed > reserved, and a zero reservation, both reject before creation.
    // SAFETY: private fixture directory.
    assert!(matches!(unsafe { GrowableMappedFile::create(&fixture.path, 64, 65) }, Err(MappingError::Bounds)));
    // SAFETY: private fixture directory.
    assert!(matches!(unsafe { GrowableMappedFile::create(&fixture.path, 0, 0) }, Err(MappingError::Bounds)));

    // An empty committed region is a valid OPEN owner, not a closed one.
    // SAFETY: private fixture directory.
    let mut file = unsafe { GrowableMappedFile::create(&fixture.path, 256, 0) }.unwrap();
    assert_eq!(file.len(), 0);
    assert!(file.is_empty() && file.is_open() && file.as_slice().is_empty());
    assert!(matches!(file.read(0, 1), Err(MappingError::Bounds)));
    file.grow_to(64).unwrap();
    assert_eq!(file.len(), 64);
    file.write(63, &[42]).unwrap();
    assert_eq!(file.read(63, 1).unwrap(), &[42]);

    // Bounded projections, with a reported truncation rather than a silent cut.
    let mut text = [0u8; 96];
    let mut truncated = false;
    assert!(file.to_string(&mut text, &mut truncated));
    assert!(!truncated);
    let rendered = std::str::from_utf8(&text).unwrap();
    assert!(rendered.starts_with("GrowableMappedFile"), "{rendered}");
    let mut tiny = [0u8; 8];
    let mut cut = false;
    assert!(!file.to_string(&mut tiny, &mut cut));
    assert!(cut);
    let mut fields = [0u8; 192];
    let mut cut_fields = false;
    assert!(file.to_string_struct(&mut fields, &mut cut_fields));
    let dumped = std::str::from_utf8(&fields).unwrap();
    assert!(dumped.contains("reserved=") && dumped.contains("committed="), "{dumped}");
}

#[test]
fn reopen_maps_the_same_file_without_resizing_it() {
    if !GrowableMappedFile::supported() {
        return; // no reservation backend on this host: open rejects, proven below
    }
    let fixture = Fixture::new();
    let reserved = 4096u64;
    let length_before;
    {
        // SAFETY: private fixture directory; nothing else opens or truncates it.
        let mut file = unsafe { GrowableMappedFile::create(&fixture.path, reserved, 512) }.unwrap();
        file.write(0, &[0xAB; 512]).unwrap();
        file.sync().unwrap();
        file.close();
        length_before = fs::metadata(&fixture.path).unwrap().len();
        assert_eq!(length_before, reserved, "create fixes the reservation as the file's length");
    }

    // Reopening is not creating — a missing file is refused, never silently made.
    let missing = Fixture::new();
    // SAFETY: private fixture directory; this path does not exist.
    assert!(unsafe { GrowableMappedFile::open(&missing.path, reserved, 512) }.is_err());

    // A file too short to hold the reservation is refused rather than mapped as
    // something it is not, and the refusal does not resize it.
    let short = Fixture::new();
    fs::write(&short.path, vec![0u8; 128]).unwrap();
    // SAFETY: private fixture directory; no other writer.
    assert!(matches!(unsafe { GrowableMappedFile::open(&short.path, reserved, 0) },
                     Err(MappingError::Bounds)));
    assert_eq!(fs::metadata(&short.path).unwrap().len(), 128);

    // committed > reserved rejects before the file is touched, exactly as create does.
    // SAFETY: private fixture directory; the file exists.
    assert!(matches!(unsafe { GrowableMappedFile::open(&fixture.path, 64, 65) },
                     Err(MappingError::Bounds)));

    // The real reopen: the same bytes come back and the file is NOT resized by opening.
    // SAFETY: private fixture directory; nothing else opens or truncates it.
    let mut file = unsafe { GrowableMappedFile::open(&fixture.path, reserved, 512) }.unwrap();
    assert_eq!(fs::metadata(&fixture.path).unwrap().len(), length_before, "open never resizes");
    assert_eq!(file.len(), 512);
    assert_eq!(file.reserved(), reserved as usize);
    assert!(file.is_open());
    assert_eq!(file.read(0, 512).unwrap(), &[0xAB; 512][..], "the bytes came back");

    // Raising the extent back to what the file already holds must be FREE: no second
    // reservation past the physical end, and the mapping still does not move.
    let before = file.as_slice().as_ptr();
    let allocated_before = file.allocated_bytes().unwrap();
    file.grow_to(2048).unwrap();
    assert_eq!(file.len(), 2048);
    assert_eq!(file.as_slice().as_ptr(), before, "a re-grow must not move the mapping");
    assert!(file.allocated_bytes().unwrap() >= 2048);
    assert!(file.allocated_bytes().unwrap() >= allocated_before);
    assert_eq!(file.read(0, 512).unwrap(), &[0xAB; 512][..], "bytes survive the re-grow");

    // A committed region the filesystem cannot back is refused: a file created with
    // no allocated prefix cannot be opened asking for a page of it.
    let bare = Fixture::new();
    // SAFETY: private fixture directory; nothing else opens it.
    let mut sparse = unsafe { GrowableMappedFile::create(&bare.path, reserved, 0) }.unwrap();
    assert!(sparse.allocated_bytes().unwrap() < 512, "nothing is allocated yet");
    sparse.close();
    // SAFETY: private fixture directory; no other writer.
    assert!(matches!(unsafe { GrowableMappedFile::open(&bare.path, reserved, 512) },
                     Err(MappingError::Preallocation(_))));

    file.write(600, &[1, 2, 3]).unwrap();
    assert_eq!(file.read(600, 3).unwrap(), &[1, 2, 3]);
    file.sync().unwrap();
    file.close();

    // A second reopen sees that write, so reopening is a real round trip and not a
    // fresh mapping of a stale file.
    // SAFETY: private fixture directory; nothing else opens it.
    let mut again = unsafe { GrowableMappedFile::open(&fixture.path, reserved, 2048) }.unwrap();
    assert_eq!(again.read(600, 3).unwrap(), &[1, 2, 3]);
    assert!(matches!(again.write(4096, &[1]), Err(MappingError::Bounds)),
            "still bounded by the reservation");
    again.close();
}

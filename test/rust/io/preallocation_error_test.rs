//! Error owner: all diagnostic forms, exact underlying OS codes and both failures.
use relational_engine_scratchpad::PreallocationError;
use std::{error::Error, io};

#[test]
fn every_error_preserves_observable_causes() {
    for (error, expected) in [
        (PreallocationError::Closed, "preallocated file is closed"),
        (PreallocationError::Length, "preallocation requires a nonzero representable length"),
        (PreallocationError::Unsupported, "disk preallocation is unsupported on this platform"),
        (PreallocationError::Incomplete { requested: 9, allocated: 8 }, "disk reservation incomplete: requested 9, allocated 8"),
    ] {
        assert_eq!(error.to_string(), expected);
        assert!(error.source().is_none());
    }
    let os = io::Error::from_raw_os_error(28);
    let error = PreallocationError::from(os);
    assert!(matches!(error, PreallocationError::Io(ref cause) if cause.raw_os_error() == Some(28)));
    assert!(error.source().is_some());
    let both = PreallocationError::Cleanup {
        operation: Box::new(PreallocationError::Incomplete { requested: 9, allocated: 8 }),
        cleanup: io::Error::new(io::ErrorKind::PermissionDenied, "cleanup denied"),
    };
    assert_eq!(both.to_string(), "disk reservation incomplete: requested 9, allocated 8; failed-file cleanup: cleanup denied");
    assert_eq!(both.source().unwrap().to_string(), "disk reservation incomplete: requested 9, allocated 8");
    assert!(!format!("{both:?}").is_empty());
}

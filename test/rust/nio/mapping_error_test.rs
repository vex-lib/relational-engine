//! MappingError owner: every variant's observable diagnostic and retained OS cause.
use relational_engine_scratchpad::{MappingError, PreallocationError};
use std::error::Error;

#[test]
fn diagnostics_and_os_error_source() {
    for (error, expected) in [
        (MappingError::Closed, "mapping is closed"),
        (MappingError::ReadOnly, "mapping is read-only"),
        (MappingError::Bounds, "mapping range is out of bounds"),
        (MappingError::Length, "mapping length is not representable"),
        (MappingError::NotRegularFile, "mapping requires a regular file"),
    ] {
        assert_eq!(error.to_string(), expected);
        assert!(error.source().is_none());
        assert!(!format!("{error:?}").is_empty());
    }
    let error = MappingError::from(std::io::Error::new(std::io::ErrorKind::PermissionDenied, "denied fixture"));
    assert_eq!(error.to_string(), "mapping IO: denied fixture");
    assert_eq!(error.source().unwrap().to_string(), "denied fixture");
    assert!(matches!(error, MappingError::Io(ref source) if source.kind() == std::io::ErrorKind::PermissionDenied));
    let error = MappingError::Preallocation(PreallocationError::Closed);
    assert_eq!(error.to_string(), "mapping preallocation: preallocated file is closed");
    assert_eq!(error.source().unwrap().to_string(), "preallocated file is closed");
}

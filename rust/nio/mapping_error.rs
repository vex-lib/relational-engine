//! DEFINITION: MappingError reports fixed-extent mapping admission and access failures.
//! OVERVIEW: Closed, ReadOnly, Bounds, Length, NotRegularFile, Io(std::io::Error),
//! Preallocation(PreallocationError) for retained-descriptor admission failures.
//! Public: Display/error source preserve OS diagnostics without synchronous logging.
use crate::annotation::{definition, overview};

#[definition]
#[overview]
#[derive(Debug)]
pub enum MappingError {
    Closed,
    ReadOnly,
    Bounds,
    Length,
    NotRegularFile,
    Io(std::io::Error),
    Preallocation(crate::io::preallocation_error::PreallocationError),
}

impl std::fmt::Display for MappingError {
    /// Render a recoverable error; this does not print or terminate.
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Closed => formatter.write_str("mapping is closed"),
            Self::ReadOnly => formatter.write_str("mapping is read-only"),
            Self::Bounds => formatter.write_str("mapping range is out of bounds"),
            Self::Length => formatter.write_str("mapping length is not representable"),
            Self::NotRegularFile => formatter.write_str("mapping requires a regular file"),
            Self::Io(error) => write!(formatter, "mapping IO: {error}"),
            Self::Preallocation(error) => write!(formatter, "mapping preallocation: {error}"),
        }
    }
}

impl std::error::Error for MappingError {
    /// Retain the original OS error for caller classification and diagnostics.
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(error) => Some(error),
            Self::Preallocation(error) => Some(error),
            _ => None,
        }
    }
}

impl From<std::io::Error> for MappingError {
    /// Convert an OS failure without discarding its kind or native error code.
    fn from(error: std::io::Error) -> Self { Self::Io(error) }
}

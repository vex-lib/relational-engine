//! DEFINITION: PreallocationError reports create-new/reservation failures without
//! hiding OS causes or failure-cleanup errors. This is recoverable cold IO, not THROW.
//! OVERVIEW: Closed, Length, Unsupported, Incomplete { requested, allocated },
//! Io(Error), Cleanup { operation: Box<PreallocationError>, cleanup: Error }.
//! Public: Display, Error::source and From<io::Error>. Error formatting/boxing may
//! allocate under Rust's ordinary cold abort-on-OOM policy.
use crate::annotation::{definition, overview};

#[definition]
#[overview]
#[derive(Debug)]
pub enum PreallocationError {
    Closed,
    Length,
    Unsupported,
    Incomplete { requested: u64, allocated: u64 },
    Io(std::io::Error),
    Cleanup { operation: Box<PreallocationError>, cleanup: std::io::Error },
}

impl std::fmt::Display for PreallocationError {
    /// Render the failure, including both operation and cleanup when applicable.
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Closed => formatter.write_str("preallocated file is closed"),
            Self::Length => formatter.write_str("preallocation requires a nonzero representable length"),
            Self::Unsupported => formatter.write_str("disk preallocation is unsupported on this platform"),
            Self::Incomplete { requested, allocated } =>
                write!(formatter, "disk reservation incomplete: requested {requested}, allocated {allocated}"),
            Self::Io(error) => write!(formatter, "preallocation IO: {error}"),
            Self::Cleanup { operation, cleanup } =>
                write!(formatter, "{operation}; failed-file cleanup: {cleanup}"),
        }
    }
}

impl std::error::Error for PreallocationError {
    /// Expose the original cause; cleanup errors remain separately inspectable.
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(error) => Some(error),
            Self::Cleanup { operation, .. } => Some(operation.as_ref()),
            _ => None,
        }
    }
}

impl From<std::io::Error> for PreallocationError {
    /// Preserve error kind and native error code instead of returning bare false.
    fn from(error: std::io::Error) -> Self { Self::Io(error) }
}

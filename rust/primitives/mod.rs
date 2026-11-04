//! Byte-backed primitive values. Each named type owns one file.
pub mod string;
pub mod atomic_string;
mod history;
mod snapshot;

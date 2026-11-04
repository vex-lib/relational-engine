//! Canonical annotation vocabulary — the Rust form of the C `src/annotation/*.h`
//! marker set (the Two-Semicolon Annotation Style Law).
//!
//! The markers are defined once by the `relational-annotations` proc-macro crate;
//! this module is their single in-crate home, so engine files write
//! `use crate::annotation::{overview, intention};` and then `#[overview]`. Only
//! the spelling differs from C (`;;overview`); the marker set is identical.
//!
//! ```ignore
//! use crate::annotation::{intention, overview, what};
//!
//! #[overview]
//! pub struct Widget;
//!
//! #[what("u64")]
//! pub type Id = u64;
//!
//! #[intention("why this exists")]
//! pub fn step() {}
//! ```

pub use relational_annotations::{
    checker, definition, draft, getter, hotcode, incomplete, inherits, intention, overview,
    platform_exclusive, setter, validator, what,
};

//! Zero-dependency marker attributes shared with the C `;;` annotations.
//!
//! The marker set is identical across languages (the Two-Semicolon Annotation
//! Style Law); only the spelling differs — C uses `;;NAME` (a macro expanding to
//! `_Static_assert`), Rust uses `#[name]` (a proc-macro passthrough that
//! validates its optional argument and compiles to nothing at runtime). These
//! are real attributes, not lookalike doc comments.
//!
//! # The marker set
//!
//! Fourteen markers, one vocabulary, two spellings.
//!
//! | C (`annotation/annotation.h`) | Rust | Argument |
//! |---|---|---|
//! | `;;CHECKER` | `#[checker]` | none |
//! | `;;VALIDATOR` | `#[validator]` | none |
//! | `;;DEFINITION` | `#[definition]` | none |
//! | `;;DRAFT` | `#[draft]` | none |
//! | `;;GETTER` | `#[getter]` | none |
//! | `;;HOTCODE` | `#[hotcode]` | none |
//! | `;;INCOMPLETE` | `#[incomplete]` | none |
//! | `;;INHERITS(text)` | `#[inherits("text")]` | one string |
//! | `;;INTENTION(text)` | `#[intention("text")]` | one string |
//! | `;;OVERVIEW` | `#[overview]` | none |
//! | `;;PLATFORM_EXCLUSIVE(p)` | `#[platform_exclusive("p")]` | one string |
//! | `;;SETTER` | `#[setter]` | none |
//! | `;;TEST` | `#[test]` | none |
//! | `;;WHAT(text)` | `#[what("text")]` | one string |
//!
//! # Why `;;TEST` has no macro here
//!
//! `#[test]` is built into Rust and is the attribute that actually registers a
//! test. Defining a proc macro named `test` would shadow it in any file that
//! imports this crate, silently turning every `#[test]` into a passthrough that
//! runs nothing. The meaning of `;;TEST` and `#[test]` is already the same — a
//! function owned by the test suite — so the equation is spelling-only and the
//! built-in is the correct Rust form.
//!
//! # Diagnostic parity
//!
//! Every marker reports its C identity (`@Overview`, not `overview`) so a single
//! grep finds an annotation in either language.
//!
//! ```ignore
//! use relational_annotations::{overview, intention};
//!
//! #[overview]
//! pub struct Widget;
//!
//! #[intention("why this exists")]
//! pub fn step() {}
//! ```

use proc_macro::{TokenStream, TokenTree};

// Emit a single compile-time error so a malformed annotation fails loudly
// instead of being silently accepted.
/// Build a compile_error token stream naming the malformed annotation and reason.
fn invalid(name: &str, message: &str) -> TokenStream {
    let text = format!("compile_error!({:?});", format!("{name}: {message}"));
    text.parse().expect("compile_error! is valid tokens")
}

/// Pass the annotated item through only when the attribute has no arguments.
fn passthrough_no_args(name: &str, attr: TokenStream, item: TokenStream) -> TokenStream {
    if attr.is_empty() {
        item
    } else {
        invalid(name, "takes no arguments")
    }
}

/// Pass the annotated item through only when the attribute has one string literal argument.
fn passthrough_string(name: &str, attr: TokenStream, item: TokenStream) -> TokenStream {
    let mut trees = attr.into_iter();
    match (trees.next(), trees.next()) {
        (Some(TokenTree::Literal(literal)), None) if literal.to_string().starts_with('"') => item,
        _ => invalid(name, "requires exactly one string literal argument"),
    }
}

macro_rules! no_arg_marker {
    ($($fn_name:ident => $label:literal),* $(,)?) => {
        $(
            #[proc_macro_attribute]
            /// Validate a marker that accepts no arguments, then return its item unchanged.
            pub fn $fn_name(attr: TokenStream, item: TokenStream) -> TokenStream {
                passthrough_no_args($label, attr, item)
            }
        )*
    };
}

no_arg_marker! {
    checker => "@Checker",
    definition => "@Definition",
    draft => "@Draft",
    getter => "@Getter",
    hotcode => "@HotCode",
    incomplete => "@Incomplete",
    overview => "@Overview",
    setter => "@Setter",
    validator => "@Validator",
}

macro_rules! string_marker {
    ($($fn_name:ident => $label:literal),* $(,)?) => {
        $(
            #[proc_macro_attribute]
            /// Validate a marker's single string argument, then return its item unchanged.
            pub fn $fn_name(attr: TokenStream, item: TokenStream) -> TokenStream {
                passthrough_string($label, attr, item)
            }
        )*
    };
}

string_marker! {
    inherits => "@Inherits",
    intention => "@Intention",
    platform_exclusive => "@PlatformExclusive",
    what => "@What",
}

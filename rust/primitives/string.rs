//! DEFINITION: primitives/string projects Bytes without Rust character storage.
//! OVERVIEW: no owned class. to_byte_array borrows; as_text validates UTF-8.
//! Byte storage permits arbitrary Bytes; text projection rejects invalid UTF-8.
/// Borrow a string's UTF-8 representation as bytes without copying or allocating.
pub fn to_byte_array(text: &str) -> &[u8] { text.as_bytes() }
/// Interpret bytes as UTF-8, returning the standard validation error for malformed input.
pub fn as_text(bytes: &[u8]) -> Result<&str, std::str::Utf8Error> {
    std::str::from_utf8(bytes)
}

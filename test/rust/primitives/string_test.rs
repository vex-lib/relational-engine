// Owner proof for primitives/string; also prove the old text compatibility path.
use relational_engine_scratchpad::{bytes, primitives::string};

/// Checks UTF-8 byte/text conversion, malformed input rejection, and legacy module aliases.
#[test]
fn byte_text_contract() {
    assert_eq!(string::to_byte_array("hello"), bytes!("hello"));
    assert_eq!(string::as_text(bytes!("🌍")).unwrap(), "🌍");
    assert_eq!(string::as_text(b"").unwrap(), "");
    assert!(string::as_text(&[0xff]).is_err());
    assert_eq!(relational_engine_scratchpad::string::to_byte_array("hello"), b"hello");
    assert_eq!(relational_engine_scratchpad::text::string::to_byte_array("hello"), b"hello");
}

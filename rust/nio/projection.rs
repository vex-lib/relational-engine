//! Cold bounded string projection shared by storage classes. Formatting may allocate.
//! A short output is NUL-terminated when possible, flagged, and returns false.
/// Copy formatted text into a bounded byte slice, NUL-terminating and flagging truncation.
pub(crate) fn write(text: String, dest: &mut [u8], out_truncated: &mut bool) -> bool {
    let available = dest.len().saturating_sub(1);
    let copied = available.min(text.len());
    *out_truncated = text.len() > available || dest.is_empty();
    if !dest.is_empty() {
        dest[..copied].copy_from_slice(&text.as_bytes()[..copied]);
        dest[copied] = 0;
    }
    !*out_truncated
}

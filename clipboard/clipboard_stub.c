#include "vex.h"
#include "clipboard/clipboard.h"
#include "lang/platform.h"
// Production R2 I/O owner: Relational Engine; existing Clipboard ABI preserved.
#include <string.h>

#if !defined(VEX_PLATFORM_APPLE)

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Clipboard (platform-exclusive stub)
 * ============================================================================
 * Non-Apple fallback for the system clipboard bridge: a procedural Clipboard
 * API backed by a single static 4096-byte char buffer (s_stubText). Text
 * copy/paste round-trips in-process through that buffer with truncation at
 * the 4095-byte bound; image operations always return false because there is
 * no OS pasteboard to exchange pixels with. Zero allocation, zero threads,
 * zero steady-state cost — the stub exists so R4/R5 callers compile and run
 * on Linux/Windows while the macOS NSPasteboard implementation carries the
 * real bridge. The whole file is compiled out under VEX_PLATFORM_APPLE.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Clipboard_stub (io/clipboard_stub.c)
 * ============================================================================
 * Platform-exclusive stub for the system clipboard bridge on non-Apple
 * systems without NSPasteboard. Procedural Clipboard API backed by a static
 * char buffer; image operations are unsupported and always return false.
 *
 * STRUCT FIELDS: none — procedural (static char s_stubText[4096] backing store)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Getters:
 *   - Clipboard_hasText()                    : true when text is stored
 *   - Clipboard_hasImage()                   : always false (no pasteboard)
 *   - Clipboard_getText(dest, maxBytes)          : copy stored text, Bytes written
 *   - Clipboard_getImage(outWidth, outHeight, dest, maxBytes) : always false
 *
 * Setters:
 *   - Clipboard_setText(text)                    : store text (truncated at 4095)
 *   - Clipboard_setImage(pixels, width, height)  : always false
 *   - Clipboard_clear()                      : empty the text store
 * ============================================================================
 */

static char s_stubText[4096] = {0};

/** Report whether the non-Apple stub currently contains nonempty text. */
bool Clipboard_hasText() {
    return s_stubText[0] != '\0';
}

/** Report that the non-Apple clipboard stub has no image format support. */
bool Clipboard_hasImage() {
    return false;
}

/** Copy the stub's stored text into the bounded caller buffer and return bytes copied. */
bytes Clipboard_getText(char *dest, bytes maxBytes) {
    if (dest == nullptr || maxBytes == 0) return 0;
    bytes len = strlen(s_stubText);
    if (len >= maxBytes) len = maxBytes - 1;
    memcpy(dest, s_stubText, len);
    dest[len] = '\0';
    return len;
}

/** Store text in the fixed stub buffer, truncating to its maximum NUL-terminated length. */
bool Clipboard_setText(const char *text) {
    if (text == nullptr) return false;
    bytes len = strlen(text);
    if (len >= sizeof(s_stubText)) len = sizeof(s_stubText) - 1;
    memcpy(s_stubText, text, len);
    s_stubText[len] = '\0';
    return true;
}

/** Report that image retrieval is unsupported by the non-Apple clipboard stub. */
bool Clipboard_getImage(i32 *outWidth, i32 *outHeight, any dest, bytes maxBytes) {
    (fn) dest; (fn) maxBytes; (fn) outWidth; (fn) outHeight;
    return false;
}

/** Report that image publication is unsupported by the non-Apple clipboard stub. */
bool Clipboard_setImage(const_any pixels, i32 width, i32 height) {
    (fn) pixels; (fn) width; (fn) height;
    return false;
}

/** Empty the stub's stored text buffer. */
fn Clipboard_clear() {
    s_stubText[0] = '\0';
}

#endif

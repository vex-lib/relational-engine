#ifndef IO_CLIPBOARD_H
#define IO_CLIPBOARD_H


#include "vex.h"
#include "lang/primitive.h"

// clipboard/clipboard.h — System Clipboard & Pasteboard Bridge.
//
// Provides zero-allocation bridging between off-heap engine buffers and the
// operating system clipboard (NSPasteboard on macOS, Wayland/X11 on Linux).
//
// Supports:
// 1. UTF-8 Plain Text copy/paste.
// 2. Raw RGBA8 2D Bitmap copy/paste.
// 3. Clear and format inspection.

typedef struct ClipboardImage {
    i32 width;
    i32 height;
    bytes  byteLength;
    u8 *pixels; // RGBA8 packed pixels
} ClipboardImage;

bool   Clipboard_hasText();
bool   Clipboard_hasImage();

bytes Clipboard_getText(char *dest, bytes maxBytes);

bool   Clipboard_setText(const char *text);

bool   Clipboard_getImage(i32 *outWidth, i32 *outHeight, void *dest, bytes maxBytes);

bool   Clipboard_setImage(const void *pixels, i32 width, i32 height);

void   Clipboard_clear();

#endif

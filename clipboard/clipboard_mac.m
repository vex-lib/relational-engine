#import <AppKit/AppKit.h>
#include "clipboard/clipboard.h"
// Production R2 I/O owner: Relational Engine; native pasteboard ABI preserved.
#include "lang/annotation.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Clipboard (macOS bridge)
 * ============================================================================
 * Native NSPasteboard bridge supporting UTF-8 text and raw RGBA8 bitmaps:
 * Clipboard_getText copies into a caller-owned buffer (truncation-safe),
 * Clipboard_getImage decodes TIFF/PNG pasteboard data into caller-owned
 * pixel memory with outputs-first width/height outputs, and Clipboard_setImage
 * encodes RGBA8 through NSBitmapImageRep. Zero engine allocation — all
 * buffers are the caller's. This is the L4 platform seam; Linux/Wayland
 * builds provide a sibling implementation of the same clipboard/clipboard.h
 * contract. Lives at R2 as a leaf I/O behavior.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Clipboard_mac (io/clipboard_mac.m)
 * ============================================================================
 * Native macOS NSPasteboard bridge supporting UTF-8 text and raw RGBA bitmaps.
 *
 * STRUCT FIELDS (Mirroring clipboard/clipboard.h):
 * ----------------------------------------------------------------------------
 *   ClipboardImage {
 *     i32 width;      // pixel width of the RGBA8 bitmap
 *     i32 height;     // pixel height of the RGBA8 bitmap
 *     bytes byteLength;  // width * height * 4
 *     u8 *pixels;    // RGBA8 packed pixels
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Core Functions: (.h)
 *   - Clipboard_hasText(void)
 *   - Clipboard_hasImage(void)
 *   - Clipboard_getText(dest, maxBytes)
 *   - Clipboard_setText(text)
 *   - Clipboard_getImage(outWidth, outHeight, dest, maxBytes)
 *   - Clipboard_setImage(pixels, width, height)
 *   - Clipboard_clear(void)
 * ============================================================================
 */
;;PLATFORM_EXCLUSIVE("Mac")
/** Report whether the system pasteboard advertises its plain-text format. */
bool Clipboard_hasText(void) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSArray *types = [pb types];
        return [types containsObject:NSPasteboardTypeString];
    }
}

/** Report whether the system pasteboard advertises TIFF or PNG image data. */
bool Clipboard_hasImage(void) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSArray *types = [pb types];
        return [types containsObject:NSPasteboardTypeTIFF] ||
               [types containsObject:NSPasteboardTypePNG];
    }
}

/** Copy pasteboard UTF-8 text into the caller buffer, NUL-terminating and truncating to its capacity. */
bytes Clipboard_getText(char *dest, bytes maxBytes) {
    if (dest == nullptr || maxBytes == 0) {
        return 0;
    }
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSString *str = [pb stringForType:NSPasteboardTypeString];
        if (str == nil) {
            dest[0] = '\0';
            return 0;
        }

        const char *utf8 = [str UTF8String];
        if (utf8 == nullptr) {
            dest[0] = '\0';
            return 0;
        }

        bytes len = strlen(utf8);
        if (len >= maxBytes) {
            len = maxBytes - 1;
        }
        memcpy(dest, utf8, len);
        dest[len] = '\0';
        return len;
    }
}

/** Replace pasteboard contents with the supplied UTF-8 text. */
bool Clipboard_setText(const char *text) {
    if (text == nullptr) {
        return false;
    }
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        NSString *str = [NSString stringWithUTF8String:text];
        if (str == nil) {
            return false;
        }
        return [pb setString:str forType:NSPasteboardTypeString];
    }
}

/** Decode pasteboard TIFF/PNG into caller RGBA storage when it fits, then return dimensions. */
bool Clipboard_getImage(i32 *outWidth, i32 *outHeight, void *dest, bytes maxBytes) {
    if (dest == nullptr || outWidth == nullptr || outHeight == nullptr) {
        return false;
    }
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSData *data = [pb dataForType:NSPasteboardTypeTIFF];
        if (data == nil) {
            data = [pb dataForType:NSPasteboardTypePNG];
        }
        if (data == nil) {
            return false;
        }

        NSBitmapImageRep *rep = [NSBitmapImageRep imageRepWithData:data];
        if (rep == nil) {
            return false;
        }

        i32 w = (i32) [rep pixelsWide];
        i32 h = (i32) [rep pixelsHigh];
        bytes needed = (bytes) w * (bytes) h * 4;
        if (needed > maxBytes) {
            return false;
        }

        *outWidth = w;
        *outHeight = h;

        unsigned char *bitmapData = [rep bitmapData];
        if (bitmapData != nullptr) {
            memcpy(dest, bitmapData, needed);
            return true;
        }
        return false;
    }
}

/** Encode caller-provided RGBA pixels as TIFF and replace pasteboard image contents. */
bool Clipboard_setImage(const void *pixels, i32 width, i32 height) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    @autoreleasepool {
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nullptr
                          pixelsWide:width
                          pixelsHigh:height
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSCalibratedRGBColorSpace
                         bytesPerRow:width * 4
                        bitsPerPixel:32];

        if (rep == nil) {
            return false;
        }

        unsigned char *destData = [rep bitmapData];
        if (destData == nullptr) {
            return false;
        }
        memcpy(destData, pixels, (bytes) width * (bytes) height * 4);

        NSData *tiffData = [rep TIFFRepresentation];
        if (tiffData == nil) {
            return false;
        }

        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        return [pb setData:tiffData forType:NSPasteboardTypeTIFF];
    }
}

/** Clear all items from the system pasteboard. */
void Clipboard_clear(void) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
    }
}

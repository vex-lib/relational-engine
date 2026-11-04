#ifndef IO_LOGKIND_H
#define IO_LOGKIND_H

// io/logkind.h — the LogKind taxonomy, ported from io/LogKind.java.
//
// Numeric event kinds for Log. Payload slots are documented per kind;
// coordinates are stored as fixed-point (value * 1000) so parser output stays
// readable.

#define LOG_KIND_RENDER_PRODUCE 1    // v0=slot, v1=drawCount
#define LOG_KIND_RENDER_PRESENT 2    // v0=slot, v1=presentCount
#define LOG_KIND_RENDER_DROPPED 3    // v0=slot, v1=reason (0=skipped, 1=_out-of-date)

#define LOG_KIND_KEY_DOWN   10       // v0=keyCode, v1=exactNanos
#define LOG_KIND_KEY_REPEAT 11       // v0=keyCode, v1=exactNanos
#define LOG_KIND_KEY_UP     12       // v0=keyCode, v1=exactNanos

#define LOG_KIND_MOUSE_DOWN 20       // v0=button, v1=x*1000, v2=y*1000
#define LOG_KIND_MOUSE_UP   21       // v0=button, v1=x*1000, v2=y*1000
#define LOG_KIND_MOUSE_MOVE 22       // v0=0, v1=x*1000, v2=y*1000

#define LOG_KIND_TOUCH_DOWN    30    // v0=touchId, v1=x*1000, v2=y*1000, v3=pressure*1000
#define LOG_KIND_TOUCH_UP      31    // v0=touchId, v1=x*1000, v2=y*1000
#define LOG_KIND_TOUCH_MOVE    32    // v0=touchId, v1=x*1000, v2=y*1000, v3=pressure*1000
#define LOG_KIND_TOUCH_CANCEL  33    // v0=touchId

#endif
#define NIO_MEM_IMPLEMENTATION

#include "vex.h"
#include "nio/mem.h"
#include "debug/throw.h"

#include <stdlib.h>
#include <string.h>

#if defined(VEX_PLATFORM_WINDOWS_X86_64) || defined(VEX_PLATFORM_WINDOWS_ARM64)
#include <io.h>
#else
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#endif

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ForeignMemory
 * ============================================================================
 * Tier 3 of the relational memory architecture (Pool to Arena to any).
 * Growable OS page chunks are owned by pool.c; logical arenas are owned by
 * arena.c; transient scratch is owned by transient.c; this file owns the
 * process-wide Memory_* VarHandle operations, the zero-alloc crash-dump fault
 * reporter with tip diagnostics, and the async-signal-safe crash handler.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ForeignMemory (nio/mem)
 * ============================================================================
 * VarHandle allocation API: allocation, reallocation, fail-fast free routing,
 * crash-dump reporting with tip diagnostics, metadata reads, and lifetime
 * classification. The OS page pool lives in pool.c; arenas live in arena.c;
 * scratch allocations live in transient.c.
 * ============================================================================
 */

static au32 s_faultPolicy = (u32) MEMORY_FAULT_ABORT;

/** Write raw bytes directly to stderr without heap allocation or stdio buffering. */
static fn writeStderrBytes(const u8 *data, bytes len)
{
    if (!data || len == 0u)
        return;

#if defined(VEX_PLATFORM_WINDOWS_X86_64) || defined(VEX_PLATFORM_WINDOWS_ARM64)
    (discard) _write(2, data, (u32) len);
#else
    (discard) write(STDERR_FILENO, data, len);
#endif
}

/** Write a NUL-terminated string directly to stderr without heap allocation. */
static fn writeStderrText(string text)
{
    if (!text)
        return;

    bytes len = 0u;

    while (text[len] != '\0')
        len++;

    writeStderrBytes((const u8*) text, len);
}

/** Write a 64-bit address in 0x-prefixed hexadecimal directly to stderr. */
static fn writeStderrHex64(u64 val)
{
    static const utf8 digits[16] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };

    utf8 buf[18];
    buf[0] = '0';
    buf[1] = 'x';

    for (u32 i = 0u; i < 16u; i++) {
        u32 shift = (15u - i) * 4u;
        buf[2u + i] = digits[(val >> shift) & 0xFu];
    }

    writeStderrBytes((const u8*) buf, sizeof(buf));
}

/** Dump up to 32 stack frames directly to stderr without allocating memory. */
static fn writeStderrBacktrace()
{
#if !defined(VEX_PLATFORM_WINDOWS_X86_64) && !defined(VEX_PLATFORM_WINDOWS_ARM64)
    any frames[32];
    i32 count = backtrace(frames, 32);

    if (count > 0)
        backtrace_symbols_fd(frames, count, STDERR_FILENO);
#endif
}

/** Emit a zero-alloc crash banner with fault details, tip guidance, and stack trace. */
static fn writeCrashDump(string header, string kind, const_any ptr, const MemoryArena *arena, string tip)
{
    writeStderrText("\n========================================================================\n");
    writeStderrText(header);
    writeStderrText(kind ? kind : "unknown");
    writeStderrText("\nptr:   ");
    writeStderrHex64((u64) (pointer) ptr);

    if (arena) {
        writeStderrText("\narena: ");
        writeStderrHex64((u64) (pointer) arena);
    }

    writeStderrText("\ntip:   ");
    writeStderrText(tip ? tip : "fix ownership before continuing -- do not silence free");
    writeStderrText("\nstack:\n");
    writeStderrBacktrace();
    writeStderrText("========================================================================\n");
}

#if !defined(VEX_PLATFORM_WINDOWS_X86_64) && !defined(VEX_PLATFORM_WINDOWS_ARM64)
/** Async-signal-safe crash handler for unexpected hardware faults (SIGSEGV, SIGBUS, SIGILL). */
static fn vexCrashSignalHandler(i32 sig)
{
    string kind = "hardware fault";
    string tip = "unexpected CPU/memory fault -- inspect the top stack frame and run with ASan";

    if (sig == SIGSEGV) {
        kind = "SIGSEGV (invalid memory access)";
        tip = "null, stale, or out-of-bounds dereference -- check pointer lifetime and bounds, never swallow the fault";
    } else if (sig == SIGBUS) {
        kind = "SIGBUS (alignment or unmapped page fault)";
        tip = "misaligned access or unmapped backing page -- verify 32-byte alignment and live mmap range";
    } else if (sig == SIGILL) {
        kind = "SIGILL (illegal instruction or trap)";
        tip = "hit a hardware trap or corrupted function pointer -- inspect the call site in the stack trace";
    }

    writeCrashDump("[vex] FATAL SIGNAL: ", kind, nullptr, nullptr, tip);
    (discard) raise(sig);
}
#endif

/** Set the allocator fault policy (production default is MEMORY_FAULT_ABORT). */
fn Memory_setFaultPolicy(MemoryFaultPolicy policy)
{
    atomic_store(&s_faultPolicy, (u32) policy);
}

/** Return the active allocator fault policy. */
MemoryFaultPolicy Memory_faultPolicy()
{
    return (MemoryFaultPolicy) atomic_load(&s_faultPolicy);
}

/** Install async-signal-safe crash handlers for SIGSEGV, SIGBUS, and SIGILL. */
fn Memory_installCrashHandler()
{
#if !defined(VEX_PLATFORM_WINDOWS_X86_64) && !defined(VEX_PLATFORM_WINDOWS_ARM64)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = vexCrashSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;

    (discard) sigaction(SIGSEGV, &sa, nullptr);
    (discard) sigaction(SIGBUS, &sa, nullptr);
    (discard) sigaction(SIGILL, &sa, nullptr);
#endif
}

/** Report a detected memory-safety fault; aborts in production or returns false in test policy. */
bool Memory_reportFault(string kind, const_any ptr, const MemoryArena *arena, string tip)
{
    if (Memory_faultPolicy() == MEMORY_FAULT_RETURN_FALSE) {
        THROW("memory: %s at %p (tip: %s)", kind, ptr, tip);
        return false;
    }

    writeCrashDump("[vex] FATAL MEMORY FAULT: ", kind, ptr, arena, tip);
    abort();
}

/** Return Relational Engine's own arena. */
MemoryArena *Re_arena()
{
    return Memory_defaultArena();
}

/** Allocate a typed payload in an explicit non-null arena with a 32-byte self-describing header. */
any Memory_alloc(u64 typeId, MemoryArena *arena, bytes numBytes)
{
    if (!arena)
        return nullptr;

    return MemoryArena_alloc(arena, typeId, numBytes);
}

/** Allocate a replacement in the pointer's owning arena, copy the shorter length, and free the old block. */
any Memory_realloc(any userPtr, bytes newBytes)
{
    if (!userPtr)
        return nullptr;

    if (!Memory_safeHeader(userPtr))
        return nullptr;

    MemoryArena *arena = Memory_arenaFor(userPtr);

    if (!arena)
        return nullptr;

    u64 typeId = Memory_type(userPtr);
    bytes oldLen = Memory_length(userPtr);
    any next = Memory_alloc(typeId, arena, newBytes);

    if (!next)
        return nullptr;

    memcpy(next, userPtr, oldLen < newBytes ? oldLen : newBytes);

    Memory_free(userPtr);

    return next;
}

/** Route a validated permanent block to its owning arena; nullptr and transient scratch are no-ops, invalid frees fault. */
bool Memory_free(any userPtr)
{
    if (!userPtr)
        return false;

    if (userPtr == FREED)
        return Memory_reportFault(
            "double-free on FREED sentinel",
            userPtr,
            nullptr,
            "this handle already holds FREED -- clear pointers to nullptr on release and fix the duplicate owner, do not silence free"
        );

    if (Transient_contains(userPtr))
        return false;

    pointer u = (pointer) userPtr;

    if (u < sizeof(MemoryHeader) || (u & MEMORY_ALIGN_MASK) != 0u)
        return Memory_reportFault(
            "misaligned or interior pointer on free",
            userPtr,
            nullptr,
            "only free the exact 32-byte aligned base pointer returned by Memory_alloc -- do not pass interior, stack, or literal pointers"
        );

    if (Memory_isFreed(userPtr))
        return Memory_reportFault(
            "double-free",
            userPtr,
            nullptr,
            "block was already freed -- remove the duplicate free and keep a single clear owner, do not silence free"
        );

    if (!Memory_safeHeader(userPtr))
        return Memory_reportFault(
            "foreign or corrupted pointer on free",
            userPtr,
            nullptr,
            "pointer is not a valid live arena block -- never free stack buffers, string literals, or tampered headers"
        );

    MemoryArena *a = Memory_arenaFor(userPtr);

    if (!a)
        return Memory_reportFault(
            "unowned pointer on free",
            userPtr,
            nullptr,
            "no live MemoryArena owns this address -- ensure the owning arena outlives all frees"
        );

    return MemoryArena_free(a, userPtr);
}

/** Return a validated block's payload length, or zero when the pointer is rejected. */
bytes Memory_length(any userPtr)
{
    const MemoryHeader *h = Memory_safeHeader(userPtr);

    if (h)
        return MemoryHeader_length(h);

    return 0;
}

/** Return a validated block's type id, or zero when the pointer is rejected. */
u64 Memory_type(any userPtr)
{
    const MemoryHeader *h = Memory_safeHeader(userPtr);

    if (h)
        return MemoryHeader_typeId(h);

    return 0;
}

/** Return whether both validated blocks carry the same type id. */
bool Memory_similar(const_any a, const_any b)
{
    if (!a || !b)
        return false;

    const MemoryHeader *ha = Memory_safeHeader(a);

    if (!ha)
        return false;

    const MemoryHeader *hb = Memory_safeHeader(b);

    if (!hb)
        return false;

    return MemoryHeader_typeId(ha) == MemoryHeader_typeId(hb);
}

/** Classify a pointer as transient, permanent, or unknown using registered address ranges. */
MemoryLifetime Memory_getLifetime(const_any ptr)
{
    if (!ptr)
        return MEMORY_LIFETIME_UNKNOWN;

    if (Transient_contains(ptr))
        return MEMORY_LIFETIME_TRANSIENT;

    if (Memory_arenaFor((any) ptr) != nullptr)
        return MEMORY_LIFETIME_PERMANENT;

    return MEMORY_LIFETIME_UNKNOWN;
}

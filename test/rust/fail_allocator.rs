//! Isolated owner fault injection: fail one selected allocation on this test thread.
//! No sleeps/scheduler assumptions; other threads and test-harness allocation unaffected.
use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;
thread_local! { static REMAINING: Cell<Option<usize>> = const { Cell::new(None) }; }
/// Makes the selected subsequent allocation fail on the current test thread.
pub fn fail_after(count: usize) { REMAINING.with(|value| value.set(Some(count))); }
/// Disables pending allocation failure injection on the current test thread.
pub fn reset() { REMAINING.with(|value| value.set(None)); }
struct FailAllocator;
unsafe impl GlobalAlloc for FailAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let fail = REMAINING.try_with(|value| match value.get() {
            Some(0) => { value.set(None); true }
            Some(n) => { value.set(Some(n - 1)); false }
            None => false,
        }).unwrap_or(false);
        if fail { std::ptr::null_mut() } else { unsafe { System.alloc(layout) } }
    }
    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) { unsafe { System.dealloc(pointer, layout) }; }
}
#[global_allocator]
static ALLOCATOR: FailAllocator = FailAllocator;

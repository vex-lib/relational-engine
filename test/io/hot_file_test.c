// Current HotFileSys module is a draft no-op, not a working file watcher.
// Proves safe repeated lifecycle calls only; event delivery remains unimplemented.

#include "vex.h"
#include "io/file/file.h"
int main() {
    HotFileSys_pumpEvents();
    HotFileSys_shutdown();
    for (int i = 0; i < 2; ++i) {
        HotFileSys_init();
        HotFileSys_pumpEvents();
        HotFileSys_shutdown();
    }
    return 0;
}

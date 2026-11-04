// Current HotFileSys module is a draft no-op, not a working file watcher.
// Proves safe repeated lifecycle calls only; event delivery remains unimplemented.
#include "io/hot_file.h"
int main(void) {
    HotFileSys_pumpEvents();
    HotFileSys_shutdown();
    for (int i = 0; i < 2; ++i) {
        HotFileSys_init();
        HotFileSys_pumpEvents();
        HotFileSys_shutdown();
    }
    return 0;
}

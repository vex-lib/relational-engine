#ifndef IO_HOT_FILE_H
#define IO_HOT_FILE_H

// io/hot_file.h — OS-level file monitoring and hot-reloading architecture
// ;;DRAFT

void HotFileSys_init(void);
void HotFileSys_shutdown(void);
void HotFileSys_pumpEvents(void);

#endif

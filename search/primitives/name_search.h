#ifndef SEARCH_PRIMITIVES_NAME_SEARCH_H
#define SEARCH_PRIMITIVES_NAME_SEARCH_H
#include "vex.h"

int re_name_search(const u8 *source, bytes spanBytes, bytes stride,
                   const u8 *key, bytes keyBytes, bytes *outIndex);
#endif

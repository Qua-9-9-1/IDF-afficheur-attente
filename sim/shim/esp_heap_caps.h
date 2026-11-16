#pragma once
// PC simulator: ESP-IDF capability allocator mapped on the C heap.
#include <cstdlib>

#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_INTERNAL (1 << 11)

inline void *heap_caps_malloc(size_t size, unsigned) { return malloc(size); }
inline void *heap_caps_realloc(void *p, size_t size, unsigned) { return realloc(p, size); }
inline void heap_caps_free(void *p) { free(p); }

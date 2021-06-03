#pragma once

#include "types.h"

#define ALIGN(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#define ASSERT(x) do_assert((x), #x, __FILE__, __LINE__)

void panic(const char *fmt, ...) __attribute__((noreturn));
void do_assert(const int, const char *, const char *, const int);

static inline u16 bswap16(u16 x) {
  return (x >> 8) | (x << 8);
}

static inline u32 bswap32(u32 x) {
  return ((x >> 24) | ((x & 0x00FF0000) >> 8) | ((x & 0x0000FF00) << 8) | (x << 24));
}

static inline u32 read32be(const u8 *p) {
  return p[3] | (p[2] << 8) | (p[1] << 16) | (p[0] << 24);
}

static inline u16 read16be(const u8 *p) {
  return p[1] | (p[0] << 8);
}

static inline u16 read16le(const u8 *p) {
  return p[0] | (p[1] << 8);
}

// memcpy and memset operating on words (see mem.s)
// addresses and byte count must be multiples of 4
extern void *memcpy_w(void *dst, const void *src, int n);
extern void *memset_w(void *dst, const u32 set, int n);

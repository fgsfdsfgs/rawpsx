#pragma once

#include <sys/types.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef unsigned char  u8;
typedef signed   char  s8;
typedef unsigned short u16;
typedef signed   short s16;
typedef unsigned int   u32;
typedef signed   int   s32;

typedef struct {
  u16 id;
  const char *str;
} string_t;

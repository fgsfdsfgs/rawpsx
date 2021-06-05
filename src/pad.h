#pragma once

#include "types.h"

// matches in game masks
enum input_mask_e {
  IN_DIR_RIGHT = 0x01,
  IN_DIR_LEFT  = 0x02,
  IN_DIR_DOWN  = 0x04,
  IN_DIR_UP    = 0x08,
  IN_ACTION    = 0x80,
  IN_JUMP      = 1 << 5,
  IN_PAUSE     = 1 << 6,
  IN_PASSWORD  = 1 << 7,
};

void pad_init(void);
u32 pad_get_input(void);
u32 pad_get_special_input(void);

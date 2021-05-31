#pragma once

enum game_parts_e {
  PART_BASE = 16000,
  PART_COPY_PROTECTION = 16000,
  PART_INTRO = 16001,
  PART_WATER = 16002,
  PART_PRISON = 16003,
  PART_CITE = 16004,
  PART_ARENE = 16005,
  PART_LUXE = 16006,
  PART_FINAL = 16007,
  PART_PASSWORD = 16008,
  PART_LAST = 16009
};

#define START_PART PART_INTRO

// matches in game masks
enum input_mask_e {
  IN_DIR_RIGHT = 0x01,
  IN_DIR_LEFT  = 0x02,
  IN_DIR_DOWN  = 0x04,
  IN_DIR_UP    = 0x08,
  IN_ACTION    = 0x80,
  IN_JUMP      = 1 << 5,
  IN_PAUSE     = 1 << 6,
};

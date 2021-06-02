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
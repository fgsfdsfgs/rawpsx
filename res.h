#pragma once

#include "types.h"
#include "tables.h"

#define NUM_MEMLIST_ENTRIES 146
#define MEMLIST_FILENAME    "\\DATA\\MEMLIST.BIN;1"
#define BANK_FILENAME       "\\DATA\\BANK%02X;1"
#define MEMBLOCK_SIZE       1 * 1024 * 1024

#pragma pack(push, 1)

typedef struct {
  u8 status;         // 0x0
  u8 type;           // 0x1
  u8 *bufptr;        // 0x2
  u8 rank;           // 0x6
  u8 bank;           // 0x7
  u32 bank_pos;      // 0x8
  u32 packed_size;   // 0xC
  u32 unpacked_size; // 0x12
} mementry_t;

#pragma pack(pop)

enum res_type_e {
  RT_SOUND    = 0,
  RT_MUSIC    = 1,
  RT_BITMAP   = 2, // full screen 4bpp video buffer, size=200*320/2
  RT_PALETTE  = 3, // palette (1024=vga + 1024=ega), size=2048
  RT_BYTECODE = 4,
  RT_SHAPE    = 5,
  RT_BANK     = 6, // common part shapes (bank2.mat)
};

enum res_status_e {
  RS_NULL   = 0,
  RS_LOADED = 1,
  RS_TOLOAD = 2,
};

extern u8 *res_seg_code;
extern u8 *res_seg_video[2];
extern u8 *res_seg_video_pal;
extern int res_vidseg_idx;
extern u16 res_next_part;
extern u16 res_cur_part;
extern const string_t *res_str_tab;

void res_init(void);
void res_invalidate_res(void);
void res_invalidate_all(void);
void res_setup_part(const u16 part_id);
void res_load(const u16 res_id);
const char *res_get_string(const string_t *strtab, const u16 str_id);

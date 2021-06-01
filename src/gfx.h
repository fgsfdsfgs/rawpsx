#pragma once

#include "types.h"

#define COL_ALPHA 0x10
#define COL_PAGE  0x11
#define COL_BMP   0xFF

#define ALPHA_COLOR_INDEX 12

int gfx_init(void);
void gfx_update_display(const int page);
void gfx_set_work_page(const int page);
void gfx_set_databuf(u8 *seg, const u16 ofs);
void gfx_draw_shape(u8 color, u16 zoom, s16 x, s16 y);
void gfx_fill_page(const int page, u8 color);
void gfx_copy_page(int src, int dst, s16 yscroll);
void gfx_set_palette(const u8 palnum);
void gfx_set_next_palette(const u8 palnum);
void gfx_invalidate_palette(void);
void gfx_blit_bitmap(const u8 *ptr, const u32 size);
void gfx_draw_string(const u8 col, s16 x, s16 y, const u16 strid);
void gfx_set_font(const u8 *data);
u16 gfx_get_current_palette(void);

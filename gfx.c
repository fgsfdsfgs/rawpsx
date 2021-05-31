#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <psxetc.h>
#include <psxgte.h>
#include <psxgpu.h>

#include "types.h"
#include "res.h"
#include "util.h"
#include "gfx.h"

#define PSXRGB(r, g, b) ((((b) >> 3) << 10) | (((g) >> 3) << 5) | ((r) >> 3))

// psn00b lacks MoveImage but has a VRAM2VRAM primitive, which is probably
// the same as PsyQ SDK's DR_MOVE? for some reason this macro is commented out
#define setVram2Vram( p ) ( setlen( p, 8 ), setcode( p, 0x80 ), \
  (p)->nop[0] = 0, (p)->nop[1] = 0, (p)->nop[2] = 0, (p)->nop[3] = 0 )

#define PACKET_MAX 0x10000
#define POINTS_MAX 50
#define NUM_COLORS 16
#define NUM_PAGES 4

#define BITMAP_W 320
#define BITMAP_H 200
#define BITMAP_PLANE_SIZE 8000 // 200 * 320 / 8

#define FONT_CH_W 8
#define FONT_CH_H 8
#define FONT_NUM_CH 96
#define FONT_W 64
#define FONT_H 256
#define FONT_NUM_COLS 8
#define FONT_NUM_ROWS 12
#define FONT_START_X 640
#define FONT_START_Y 0

#define PAL_START_X 640
#define PAL_START_Y 256

typedef struct {
  DISPENV disp;
  DRAWENV draw;
} fb_t;

typedef struct {
  s16 x;
  s16 y;
} vert_t;

typedef struct {
  u8 r;
  u8 g;
  u8 b;
} rgb_t;

static const rgb_t rgb_black;

static u8 gpubuffers[2][PACKET_MAX];
static u8 *gpudata;
static u8 gpubufidx = 0;

static fb_t fb[NUM_PAGES];
static fb_t *back_fb;  // buffer set to be displayed next (if not overridden)
static fb_t *front_fb; // buffer currently being displayed
static fb_t *work_fb;  // buffer that we're currently drawing to

static u8 *primptr;
static u8 *lastprim;

static u8 *gfx_data_base;
static u8 *gfx_data;

static u16 gfx_num_verts;
static vert_t gfx_verts[POINTS_MAX];

static rgb_t gfx_pal[NUM_COLORS];
static rgb_t *gfx_alphacolor = gfx_pal + ALPHA_COLOR_INDEX;
static u8 gfx_palnum;
static u8 gfx_next_palnum;

static u16 gfx_tmp_bitmap[BITMAP_W * BITMAP_H];

static u16 gfx_font_clut_data[NUM_COLORS];
static RECT gfx_font_clut_rect;
static RECT gfx_font_rect;
static int gfx_font_tpage = 0;

static inline void gpu_pushprim(const u32 size) {
  const u8 *newptr = primptr + size;
  const u8 *endptr = gpudata + PACKET_MAX;
  if (newptr > endptr) {
    printf("gpu_pushprim(): packet buffer overflow by %d bytes, resyncing\n", newptr - endptr);
    DrawSync(0);
    termPrim(lastprim);
    DrawOTag((u32 *)gpudata);
    gpubufidx ^= 1;
    gpudata = gpubuffers[gpubufidx];
    primptr = lastprim = gpudata;
  }
  catPrim(lastprim, primptr);
  lastprim = primptr;
  primptr += size;
}

static inline u8 gfx_fetch_u8(void) {
  return *(gfx_data++);
}

static inline u16 gfx_fetch_u16(void) {
  const u8 *b = gfx_data;
  gfx_data += 2;
  return (b[0] << 8) | b[1];
}

static inline fb_t *gfx_get_page(const int page) {
  if (page < NUM_PAGES)
    return &fb[page];
  switch (page) {
    case 0xFE: return front_fb;
    case 0xFF: return back_fb;
    default:   return work_fb; /* error? */
  }
}

int gfx_init(void) {
  ResetGraph(0);

  // the VM requires 4 framebuffers, 2 of which are used for double buffering
  // the VM itself decides which one to show and which one to draw to
  // FBs are aligned vertically in memory to the 256px boundary for ease of use
  SetDefDispEnv(&fb[0].disp, 0,     0, 320, 200);
  SetDefDrawEnv(&fb[0].draw, 0,     0, 320, 200);
  SetDefDispEnv(&fb[1].disp, 0,   256, 320, 200);
  SetDefDrawEnv(&fb[1].draw, 0,   256, 320, 200);
  SetDefDispEnv(&fb[2].disp, 320,   0, 320, 200);
  SetDefDrawEnv(&fb[2].draw, 320,   0, 320, 200);
  SetDefDispEnv(&fb[3].disp, 320, 256, 320, 200);
  SetDefDrawEnv(&fb[3].draw, 320, 256, 320, 200);

  // offset every DISPENV downward by 20px on the screen to account for 320x200
  fb[0].disp.screen.y = 20;
  fb[1].disp.screen.y = 20;
  fb[2].disp.screen.y = 20;
  fb[3].disp.screen.y = 20;

  // always put the CLUTs to the side of everything, in case we need 256 height
  gfx_font_clut_rect = (RECT){ PAL_START_X, PAL_START_Y, NUM_COLORS, 1 };
  // put the font somewhere to the right, should take up just one tpage
  // W divided by 2 because it's in 8-bit CLUT format
  gfx_font_rect = (RECT){ FONT_START_X, FONT_START_Y, FONT_W >> 1, FONT_H };
  // upload the font CLUT
  gfx_font_clut_data[0] = 0; // stp bit not set
  gfx_font_clut_data[1] = PSXRGB(0xFF, 0xFF, 0xFF); // pure white
  LoadImage(&gfx_font_clut_rect, (u32 *)gfx_font_clut_data);

  // initialize buffer pointers
  back_fb = gfx_get_page(1);
  front_fb = gfx_get_page(2);
  work_fb = gfx_get_page(0);

  // set default front and work buffer
  PutDispEnv(&front_fb->disp);
  PutDrawEnv(&work_fb->draw);

  // enable output
  SetDispMask(1);

  gpudata = gpubuffers[0];
  primptr = gpudata;
  lastprim = gpudata;

  gfx_palnum = gfx_next_palnum = 0xFF;

  gfx_set_font(fnt_default);

  return 0;
}

void gfx_set_databuf(u8 *seg, const u16 ofs) {
  gfx_data_base = seg;
  gfx_data = seg + ofs;
}

void gfx_set_palette(const u8 palnum) {
  if (palnum >= 32 || palnum == gfx_palnum)
    return;
  
	register const u8 *p = res_seg_video_pal + palnum * NUM_COLORS * sizeof(u16);
	for (register int i = 0; i < NUM_COLORS; ++i, p += 2) {
		const u16 color = read16be(p);
		const u8 r = (color >> 8) & 0xF;
		const u8 g = (color >> 4) & 0xF;
		const u8 b =  color       & 0xF;
		gfx_pal[i].r = (r << 4) | r;
		gfx_pal[i].g = (g << 4) | g;
		gfx_pal[i].b = (b << 4) | b;
	}

  gfx_palnum = palnum;
}

void gfx_set_next_palette(const u8 palnum) {
  gfx_next_palnum = palnum;
}

void gfx_invalidate_palette(void) {
  gfx_palnum = 0xFF;
}

void gfx_update_display(const int page) {
  DrawSync(0);

  if (primptr != gpudata) {
    termPrim(lastprim);
    DrawOTag((u32 *)gpudata);
    gpubufidx ^= 1;
    gpudata = gpubuffers[gpubufidx];
    primptr = lastprim = gpudata;
  }

  if (gfx_next_palnum != 0xFF) {
    gfx_set_palette(gfx_next_palnum);
    gfx_next_palnum = 0xFF;
  }

  fb_t *oldfb = front_fb;
  if (page != 0xFE) {
    if (page == 0xFF) {
      fb_t *tmp = front_fb;
      front_fb = back_fb;
      back_fb = tmp;
    } else {
      front_fb = gfx_get_page(page);
    }
    if (front_fb != oldfb)
      PutDispEnv(&front_fb->disp);
  }
}

void gfx_set_work_page(const int page) {
  fb_t *new = gfx_get_page(page);
  if (work_fb != new) {
    work_fb = new;
    // change drawing area to the page it wants
    // apparently you need a DR_AREA + DR_OFFSET to do this
    DR_AREA *area = (DR_AREA *)primptr;
    setDrawArea(area, &work_fb->draw.clip);
    gpu_pushprim(sizeof(DR_AREA));
    DR_OFFSET *ofs = (DR_OFFSET *)primptr;
    setDrawOffset(ofs, work_fb->draw.clip.x, work_fb->draw.clip.y);
    gpu_pushprim(sizeof(DR_OFFSET));
  }
}

static inline void gfx_push_point(const rgb_t color, const u32 stp, const s16 x, const s16 y) {
  TILE_1 *prim = (TILE_1 *)primptr;
  setTile1(prim);
  setSemiTrans(prim, stp);
  prim->x0 = x;
  prim->y0 = y;
  prim->r0 = color.r;
  prim->g0 = color.g;
  prim->b0 = color.b;
  gpu_pushprim(sizeof(TILE_1));
}

/* in the base game the polygons are always made of an even amount of vertices
 * and are representable as a list of quads
 *   2 #---# 3
 *    /     \
 * 1 #       # 4
 *    \     /
 *   0 #---# 5
 * this example divides into quads 0-5-1-4 and 1-4-2-3 (GL strip ordering)
 * generalizing, we get (i)-(n-1-i)-(i+1)-(n-2-i) / (i)-(j)-(i+1)-(j-1)
 */
static inline void gfx_push_quad_strip(const rgb_t color, const u32 stp) {
  register u16 j = gfx_num_verts - 1;
  register u16 i = 0;
  for (; i < (gfx_num_verts >> 1) - 1; ++i, --j) {
    POLY_F4 *prim = (POLY_F4 *)primptr;
    setPolyF4(prim);
    setSemiTrans(prim, stp);
    prim->r0 = color.r;
    prim->g0 = color.g;
    prim->b0 = color.b;
    // these are 32-bit in size, might as well just copy
    vert_t v0 = gfx_verts[i];
    vert_t v1 = gfx_verts[j];
    vert_t v2 = gfx_verts[i + 1];
    vert_t v3 = gfx_verts[j - 1];
    if (v1.x > v0.x) {
      prim->x0 = v0.x;
      prim->y0 = v0.y;
      prim->x1 = v1.x + 1;
      prim->y1 = v1.y;
    } else {
      prim->x0 = v1.x;
      prim->y0 = v1.y;
      prim->x1 = v0.x + 1;
      prim->y1 = v0.y;
    }
    if (v3.x > v2.x) {
      prim->x2 = v2.x;
      prim->y2 = v2.y;
      prim->x3 = v3.x + 1;
      prim->y3 = v3.y;
    } else {
      prim->x2 = v3.x;
      prim->y2 = v3.y;
      prim->x3 = v2.x + 1;
      prim->y3 = v2.y;
    }
    gpu_pushprim(sizeof(POLY_F4));
  }
}

// color == 0xFF means we have to copy an area from fb0; just use it as a texture
static inline void gfx_push_quad_strip_tex(void) {
  const u16 tpage = getTPage(2, 0, fb[0].draw.clip.x, fb[0].draw.clip.y);
  register u16 j = gfx_num_verts - 1;
  register u16 i = 0;
  for (; i < (gfx_num_verts >> 1) - 1; ++i, --j) {
    POLY_FT4 *prim = (POLY_FT4 *)primptr;
    setPolyFT4(prim);
    setSemiTrans(prim, 0);
    prim->tpage = tpage;
    prim->r0 = 0x80;
    prim->g0 = 0x80;
    prim->b0 = 0x80;
    // these are 32-bit in size, might as well just copy
    vert_t v0 = gfx_verts[i];
    vert_t v1 = gfx_verts[j];
    vert_t v2 = gfx_verts[i + 1];
    vert_t v3 = gfx_verts[j - 1];
    if (v1.x > v0.x) {
      prim->x0 = v0.x;
      prim->y0 = v0.y;
      prim->x1 = v1.x + 1;
      prim->y1 = v1.y;
    } else {
      prim->x0 = v1.x;
      prim->y0 = v1.y;
      prim->x1 = v0.x + 1;
      prim->y1 = v0.y;
    }
    if (v3.x > v2.x) {
      prim->x2 = v2.x;
      prim->y2 = v2.y;
      prim->x3 = v3.x + 1;
      prim->y3 = v3.y;
    } else {
      prim->x2 = v3.x;
      prim->y2 = v3.y;
      prim->x3 = v2.x + 1;
      prim->y3 = v2.y;
    }
    prim->u0 = prim->x0 & 0xFF;
    prim->v0 = prim->y0 & 0xFF;
    prim->u1 = prim->x1 & 0xFF;
    prim->v1 = prim->y1 & 0xFF;
    prim->u2 = prim->x2 & 0xFF;
    prim->v2 = prim->y2 & 0xFF;
    prim->u3 = prim->x3 & 0xFF;
    prim->v3 = prim->y3 & 0xFF;
    gpu_pushprim(sizeof(POLY_FT4));
  }
}

static void gfx_fill_polygon(u8 color, u16 zoom, s16 x, s16 y) {
  const u8 *p = gfx_data;
  const u16 bbw = ((*p++) * zoom) >> 6;
  const u16 bbh = ((*p++) * zoom) >> 6;
  const u16 half_bbw = (bbw >> 1);
  const u16 half_bbh = (bbh >> 1);

  const s16 x1 = x - half_bbw;
  const s16 x2 = x + half_bbw;
  const s16 y1 = y - half_bbh;
  const s16 y2 = y + half_bbh;

  if (x1 > 319 || x2 < 0 || y1 > 199 || y2 < 0)
    return;

  gfx_num_verts = *p++;
  if ((gfx_num_verts & 1) || gfx_num_verts > POINTS_MAX) {
    printf("gfx_fill_polygon(): invalid number of verts %d\n", gfx_num_verts);
    return;
  }

  const u32 stp = (color >= COL_ALPHA);
  const rgb_t rgb = stp ? *gfx_alphacolor : gfx_pal[color];

  if (gfx_num_verts == 4 && bbw == 0 && bbh <= 1) {
    gfx_push_point(rgb, stp, x, y);
    return;
  }

  for (u16 i = 0; i < gfx_num_verts; ++i) {
    gfx_verts[i].x = x1 + (((*p++) * zoom) >> 6);
    gfx_verts[i].y = y1 + (((*p++) * zoom) >> 6);
  }

  if (color == COL_PAGE)
    gfx_push_quad_strip_tex();
  else
    gfx_push_quad_strip(rgb, stp);
}

static void gfx_draw_shape_hierarchy(u16 zoom, s16 x, s16 y) {
  x -= (gfx_fetch_u8() * zoom) >> 6;
  y -= (gfx_fetch_u8() * zoom) >> 6;

  register s16 nchildren = gfx_fetch_u8();
  register u16 ofs;
  register s16 cx, cy;

  for (; nchildren >= 0; --nchildren) {
    ofs = gfx_fetch_u16();
    cx = x + ((gfx_fetch_u8() * zoom) >> 6);
    cy = y + ((gfx_fetch_u8() * zoom) >> 6);

    u16 color = 0xFF;
    if (ofs & 0x8000) {
      // TODO: sprite drawing
      color = (*gfx_data) & 0x7F;
      gfx_data += 2;
      ofs &= 0x7FFF;
    }

    u8 *bak = gfx_data;
    gfx_data = gfx_data_base + (ofs << 1);
    gfx_draw_shape(color, zoom, cx, cy);
    gfx_data = bak;
  }
}

void gfx_draw_shape(u8 color, u16 zoom, s16 x, s16 y) {
  u8 i = gfx_fetch_u8();

  if (i >= 0xC0) {
    if (color & 0x80) color = i & 0x3F;
    gfx_fill_polygon(color, zoom, x, y);
  } else {
    i &= 0x3F;
    if (i == 2)
      gfx_draw_shape_hierarchy(zoom, x, y);
  }
}

void gfx_fill_page(const int page, u8 color) {
  const fb_t *fb = gfx_get_page(page);
  FILL *prim = (FILL *)primptr;
  setFill(prim);
  prim->x0 = fb->draw.clip.x;
  prim->y0 = fb->draw.clip.y;
  prim->w = fb->draw.clip.w;
  prim->h = fb->draw.clip.h;
  prim->r0 = gfx_pal[color].r;
  prim->g0 = gfx_pal[color].g;
  prim->b0 = gfx_pal[color].b;
  gpu_pushprim(sizeof(FILL));
}

static inline void gfx_do_copy_page(fb_t *dstfb, fb_t *srcfb, const int yscroll) {
  VRAM2VRAM *prim = (VRAM2VRAM *)primptr;
  setVram2Vram(prim);
  prim->x0 = srcfb->draw.clip.x;
  prim->x1 = dstfb->draw.clip.x;
  prim->w = srcfb->draw.clip.w;
  if (yscroll < 0) {
    // chop off the top part of the source texture
    prim->y0 = srcfb->draw.clip.y - yscroll;
    prim->y1 = dstfb->draw.clip.y;
    prim->h = srcfb->draw.clip.h + yscroll;
  } else {
    // chop off the bottom part of the source texture
    prim->y0 = srcfb->draw.clip.y;
    prim->y1 = dstfb->draw.clip.y + yscroll;
    prim->h = srcfb->draw.clip.h - yscroll;
  }
  gpu_pushprim(sizeof(VRAM2VRAM));
}

void gfx_copy_page(int src, int dst, s16 yscroll) {
  if (src >= 0xFE || ((src &= ~0x40) & 0x80) == 0) {
    // no y scroll
    gfx_do_copy_page(gfx_get_page(dst), gfx_get_page(src), 0);
  } else {
    fb_t *srcfb = gfx_get_page(src & 3);
    fb_t *dstfb = gfx_get_page(dst);
    if (srcfb != dstfb && yscroll >= -199 && yscroll <= 199)
      gfx_do_copy_page(dstfb, srcfb, yscroll);
  }
}

void gfx_blit_bitmap(const u8 *ptr, const u32 size) {
  // decode; assumes amiga format
  register u16 *dst = gfx_tmp_bitmap;
  register const u8 *src = ptr;
	for (int y = 0; y < BITMAP_H; ++y) {
		for (int x = 0; x < BITMAP_W; x += 8) {
			for (int b = 0; b < 8; ++b) {
				const int mask = 1 << (7 - b);
				u8 c = 0;
				if (src[0 * BITMAP_PLANE_SIZE] & mask) c |= 1 << 0;
				if (src[1 * BITMAP_PLANE_SIZE] & mask) c |= 1 << 1;
				if (src[2 * BITMAP_PLANE_SIZE] & mask) c |= 1 << 2;
				if (src[3 * BITMAP_PLANE_SIZE] & mask) c |= 1 << 3;
				*dst++ = PSXRGB(gfx_pal[c].r, gfx_pal[c].g, gfx_pal[c].b);
			}
			++src;
		}
	}
  // dunk the pic right into the work buffer
  RECT ldrect = work_fb->draw.clip;
  ldrect.h = 200; // images are 320x200
  LoadImage(&ldrect, (u32 *)gfx_tmp_bitmap);
}

static inline void gfx_draw_char(const rgb_t col, const u32 stp, char ch, s16 x, s16 y) {
  ch -= 0x20;
  SPRT_8 *prim = (SPRT_8 *)primptr;
  setSprt8(prim);
  setSemiTrans(prim, stp);
  prim->x0 = x;
  prim->y0 = y;
  prim->r0 = col.r;
  prim->g0 = col.g;
  prim->b0 = col.b;
  prim->u0 = FONT_CH_W * (ch % FONT_NUM_COLS);
  prim->v0 = FONT_CH_H * (ch / FONT_NUM_COLS);
  prim->clut = getClut(PAL_START_X, PAL_START_Y);
  gpu_pushprim(sizeof(SPRT_8));
}

void gfx_draw_string(const u8 col, s16 x, s16 y, const u16 strid) {
  const char *str = res_get_string(NULL, strid);
  if (!str) str = res_get_string(str_tab_demo, strid);
  if (!str) {
    printf("gfx_draw_string(%d, %d, %d, %d): unknown strid\n", (int)col, (int)x, (int)y, (int)strid);
    return;
  }

  if (!gfx_font_tpage) {
    // if we aren't already drawing strings, set tpage to font
    DR_TPAGE *prim = (DR_TPAGE *)primptr;
    setDrawTPage(prim, 1, 0, getTPage(1, 0, FONT_START_X, FONT_START_Y));
    gpu_pushprim(sizeof(DR_TPAGE));
    gfx_font_tpage = 1;
  }

  const u32 stp = (col >= COL_ALPHA);
  const rgb_t rgb = stp ? *gfx_alphacolor : gfx_pal[col];
  const u16 startx = x;
  const int len = strlen(str);

	for (int i = 0; i < len; ++i) {
    if (str[i] == '\n' || str[i] == '\r') {
      y += 8;
      x = startx;
    } else {
      gfx_draw_char(rgb, stp, str[i], x * 8, y);
      ++x;
    }
	}
}

void gfx_set_font(const u8 *data) {
  u8 *out = (u8 *)gfx_tmp_bitmap;
  for (int i = 0; i < FONT_NUM_CH; ++i) {
    const int sx = FONT_CH_W * (i % FONT_NUM_COLS);
    const int sy = FONT_CH_H * (i / FONT_NUM_COLS);
    for (int y = sy; y < sy + FONT_CH_H; ++y) {
      const u8 mask = *data++;
      for (int x = 0; x < FONT_CH_W; ++x)
        out[y * FONT_W + sx + x] = (mask >> (FONT_CH_W - 1 - x)) & 1;
    }
  }
  LoadImage(&gfx_font_rect, (u32 *)gfx_tmp_bitmap);
}

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

#define BITMAP_PLANE_SIZE 8000 // 200 * 320 / 8

#define PAGE_W 320
#define PAGE_H 200

#define SCREEN_W 320
#define SCREEN_H 240

#define NUM_PAGES 4
#define NUM_BUFFERS 2
#define NUM_COLORS 16

#define PACKET_MAX 0x100
#define PALS_MAX 32
#define POINTS_MAX 50

#define PSXRGB(r, g, b) ((((b) >> 3) << 10) | (((g) >> 3) << 5) | ((r) >> 3))

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

// psn00b lacks MoveImage but has a VRAM2VRAM primitive, which is probably
// the same as PsyQ SDK's DR_MOVE? for some reason this macro is commented out
#define setVram2Vram( p ) ( setlen( p, 8 ), setcode( p, 0x80 ), \
  (p)->nop[0] = 0, (p)->nop[1] = 0, (p)->nop[2] = 0, (p)->nop[3] = 0 )

typedef struct {
  DR_TPAGE tpage;
  SPRT sprt;
} TSPRT;

typedef struct {
  DISPENV disp;
  DRAWENV draw;
  TSPRT tsprt[2];
} fb_t;

typedef struct {
  s16 x;
  s16 y;
} vert_t;

static fb_t gfx_fb[NUM_BUFFERS];
static int gfx_fb_idx;

static u8 *gfx_data_base;
static u8 *gfx_data;

static u16 gfx_num_verts;
static vert_t gfx_verts[POINTS_MAX];

static u16 gfx_pal[NUM_COLORS];
static u16 gfx_palnum;
static u16 gfx_palnum_next;
static u8 gfx_pal_uploaded = 0;

static const u8 *gfx_font;

static u8 gfx_page[NUM_PAGES][PAGE_W * PAGE_H];
static u8 *gfx_page_front;
static u8 *gfx_page_back;
static u8 *gfx_page_work;

static RECT gfx_buffer_rect = { SCREEN_W, 0, PAGE_W >> 1, PAGE_H };
static RECT gfx_pal_rect = { SCREEN_W, 256, NUM_COLORS, 1 };

static inline u8 gfx_fetch_u8(void) {
  return *(gfx_data++);
}

static inline u16 gfx_fetch_u16(void) {
  const u8 *b = gfx_data;
  gfx_data += 2;
  return (b[0] << 8) | b[1];
}

static inline u8 *gfx_get_page(const int page) {
  if (page < NUM_PAGES)
    return gfx_page[page];
  switch (page) {
    case 0xFE: return gfx_page_front;
    case 0xFF: return gfx_page_back;
    default:   return gfx_page_work; /* error? */
  }
}

int gfx_init(void) {
  ResetGraph(0);

  // set up double buffer
  SetDefDispEnv(&gfx_fb[0].disp, 0,     0, 320, 200);
  SetDefDrawEnv(&gfx_fb[1].draw, 0,     0, 320, 200);
  SetDefDispEnv(&gfx_fb[1].disp, 0,   256, 320, 200);
  SetDefDrawEnv(&gfx_fb[0].draw, 0,   256, 320, 200);

  // offset every DISPENV downward by 20px on the screen to account for 320x200
  gfx_fb[0].disp.screen.y = 20;
  gfx_fb[1].disp.screen.y = 20;

  // we're going to be blitting the screen texture by drawing two SPRTs with parts of it
  const u16 btpage1 = getTPage(1, 0, gfx_buffer_rect.x, gfx_buffer_rect.y);
  // offset by 128 and not 256 because screen texture is 8-bit
  const u16 btpage2 = getTPage(1, 0, gfx_buffer_rect.x + 128, gfx_buffer_rect.y);
  for (int i = 0; i < NUM_BUFFERS; ++i) {
    TSPRT *t1 = &gfx_fb[i].tsprt[0];
    TSPRT *t2 = &gfx_fb[i].tsprt[1];
    setDrawTPage(&t1->tpage, 1, 0, btpage1);
    setDrawTPage(&t2->tpage, 1, 0, btpage2);
    setSprt(&t1->sprt);
    setSprt(&t2->sprt);
    setSemiTrans(&t1->sprt, 0);
    setSemiTrans(&t2->sprt, 0);
    t1->sprt.clut = t2->sprt.clut = getClut(gfx_pal_rect.x, gfx_pal_rect.y);
    t1->sprt.r0 = t2->sprt.r0 = 0x80;
    t1->sprt.g0 = t2->sprt.g0 = 0x80;
    t1->sprt.b0 = t2->sprt.b0 = 0x80;
    t1->sprt.h = t2->sprt.h = PAGE_H;
    t1->sprt.w = 256;
    t2->sprt.w = PAGE_W - 256;
    t2->sprt.x0 = 256;
  }

  // initialize page pointers
  gfx_page_back = gfx_get_page(1);
  gfx_page_front = gfx_get_page(2);
  gfx_page_work = gfx_get_page(0);

  gfx_palnum = gfx_palnum_next = 0xFF;
  gfx_num_verts = 0;
  gfx_set_font(fnt_default);

  // set default front and work buffer
  gfx_fb_idx = 0;
  PutDispEnv(&gfx_fb[0].disp);
  PutDrawEnv(&gfx_fb[0].draw);
  // enable output
  SetDispMask(1);

  return 0;
}

void gfx_set_databuf(u8 *seg, const u16 ofs) {
  gfx_data_base = seg;
  gfx_data = seg + ofs;
}

static inline void gfx_load_palette(const u8 n) {
  register u16 *out = gfx_pal;
  register const u8 *p = res_seg_video_pal + n * NUM_COLORS * sizeof(u16);
  for (register int i = 0; i < NUM_COLORS; ++i, p += 2, ++out) {
    const u16 color = read16be(p);
    u8 r = (color >> 8) & 0xF;
    u8 g = (color >> 4) & 0xF;
    u8 b =  color       & 0xF;
    if (!r && !g && !b) {
      *out = 0x8000; // non-transparent black
    } else {
      r = (r << 4) | r;
      g = (g << 4) | g;
      b = (b << 4) | b;
      *out = PSXRGB(r, g, b);
    }
  }
}

void gfx_set_palette(const u8 palnum) {
  if (palnum >= PALS_MAX || palnum == gfx_palnum)
    return;
  gfx_load_palette(palnum);
  gfx_palnum = palnum;
  gfx_pal_uploaded = 0;
}

void gfx_set_next_palette(const u8 palnum) {
  gfx_palnum_next = palnum;
}

void gfx_invalidate_palette(void) {
  gfx_palnum = 0xFF;
}

u16 gfx_get_current_palette(void) {
  return gfx_palnum;
}

void gfx_update_display(const int page) {
  DrawSync(0);

  if (page != 0xFE) {
    if (page == 0xFF) {
      u8 *tmp = gfx_page_front;
      gfx_page_front = gfx_page_back;
      gfx_page_back = tmp;
    } else {
      gfx_page_front = gfx_get_page(page);
    }
  }

  if (gfx_palnum_next != 0xFF) {
    gfx_set_palette(gfx_palnum_next);
    gfx_palnum_next = 0xFF;
  }

  // upload palette to vram if needed
  if (!gfx_pal_uploaded) {
    LoadImage(&gfx_pal_rect, (u32 *)gfx_pal);
    gfx_pal_uploaded = 1;
  }
  // upload front page to the screen buffer
  LoadImage(&gfx_buffer_rect, (u32 *)gfx_page_front);
  // draw framebuffer in two parts, since it's larger than 256x256
  TSPRT *tsprt = gfx_fb[gfx_fb_idx].tsprt;
  for (int i = 0; i < 2; ++i) {
    DrawPrim(&tsprt[i].tpage);
    DrawPrim(&tsprt[i].sprt);
  }
  // now we can swap buffers
  gfx_fb_idx ^= 1;
  PutDispEnv(&gfx_fb[gfx_fb_idx].disp);
  PutDrawEnv(&gfx_fb[gfx_fb_idx].draw);
}

void gfx_set_work_page(const int page) {
  u8 *new = gfx_get_page(page);
  gfx_page_work = new;
}

static inline void gfx_draw_point(u8 color, s16 x, s16 y) {
  register const u32 ofs = y * PAGE_W + x;
  switch (color) {
    case COL_ALPHA: gfx_page_work[ofs] |= 8; break;
    case COL_PAGE:  gfx_page_work[ofs] = gfx_page[0][ofs]; break;
    default:        gfx_page_work[ofs] = color; break;
  }
}

static void gfx_draw_line_color(const u16 ofs, const u16 w, const u8 color) {
  memset(gfx_page_work + ofs, color, w);
}

static void gfx_draw_line_copy(const u16 ofs, const u16 w, const u8 color) {
  memcpy(gfx_page_work + ofs, gfx_page[0] + ofs, w);
}

static void gfx_draw_line_alpha(const u16 ofs, const u16 w, const u8 color) {
  register u8 *p = gfx_page_work + ofs;
  register const u8 *end = p + w;
  for (; p < end; ++p) *p |= 8;
}

static inline u32 gfx_fill_polygon_get_step(const vert_t *v1, const vert_t *v2, u16 *dy) {
  *dy = v2->y - v1->y;
  const u16 delta = (*dy <= 1) ? 1 : *dy;
  return ((v2->x - v1->x) * (0x4000 / delta)) << 2;
}

static void gfx_fill_polygon(u8 color, u16 zoom, s16 x, s16 y) {
  const u8 *p = gfx_data;
  const u16 bbw = ((*p++) * zoom) >> 6;
  const u16 bbh = ((*p++) * zoom) >> 6;
  const u16 half_bbw = (bbw >> 1);
  const u16 half_bbh = (bbh >> 1);

  const s16 bx1 = x - half_bbw;
  const s16 bx2 = x + half_bbw;
  const s16 by1 = y - half_bbh;
  const s16 by2 = y + half_bbh;

  if (bx1 > 319 || bx2 < 0 || by1 > 199 || by2 < 0)
    return;

  gfx_num_verts = *p++;
  if ((gfx_num_verts & 1) || gfx_num_verts > POINTS_MAX) {
    printf("gfx_fill_polygon(): invalid number of verts %d\n", gfx_num_verts);
    return;
  }

  if (gfx_num_verts == 4 && bbw == 0 && bbh <= 1) {
    gfx_draw_point(color, x, y);
    return;
  }

  void (*draw_line)(const u16, const u16, const u8);
  switch (color) {
    case COL_ALPHA:
      draw_line = gfx_draw_line_alpha;
      break;
    case COL_PAGE:
      if (gfx_page_work == gfx_page[0])
        return;
      draw_line = gfx_draw_line_copy;
      break;
    default:
      draw_line = gfx_draw_line_color;
      break;
  }

  for (u16 i = 0; i < gfx_num_verts; ++i) {
    gfx_verts[i].x = bx1 + (((*p++) * zoom) >> 6);
    gfx_verts[i].y = by1 + (((*p++) * zoom) >> 6);
  }

  s16 i = 0;
  s16 j = gfx_num_verts - 1;
  s16 x1 = gfx_verts[j].x;
  s16 x2 = gfx_verts[i].x;
  u32 cpt1 = x1 << 16;
  u32 cpt2 = x2 << 16;
  register s32 ofs = MIN(gfx_verts[i].y, gfx_verts[j].y) * PAGE_W;
  register u16 w = 0;
  register s16 xmin;
  register s16 xmax;
  for (++i, --j; gfx_num_verts; gfx_num_verts -= 2) {
    u16 h;
    const u32 step1 = gfx_fill_polygon_get_step(&gfx_verts[j + 1], &gfx_verts[j], &h);
    const u32 step2 = gfx_fill_polygon_get_step(&gfx_verts[i - 1], &gfx_verts[i], &h);
    ++i, --j;
    cpt1 = (cpt1 & 0xFFFF0000) | 0x7FFF;
    cpt2 = (cpt2 & 0xFFFF0000) | 0x8000;
    if (h == 0) {
      cpt1 += step1;
      cpt2 += step2;
    } else {
      while (h--) {
        if (ofs >= 0) {
          x1 = cpt1 >> 16;
          x2 = cpt2 >> 16;
          if (x1 < PAGE_W && x2 >= 0) {
            if (x1 < 0) x1 = 0;
            if (x2 >= PAGE_W) x2 = PAGE_W - 1;
            if (x1 > x2) { xmin = x2; xmax = x1; }
            else         { xmin = x1; xmax = x2; }
            draw_line(ofs + xmin, (xmax - xmin) + 1, color);
          }
        }
        cpt1 += step1;
        cpt2 += step2;
        ofs += PAGE_W;
        if (ofs >= PAGE_W * PAGE_H)
          return;
      }
    }
  }
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
  u8 *pagedata = gfx_get_page(page);
  memset(pagedata, color, PAGE_W * PAGE_H);
}

void gfx_copy_page(int src, int dst, s16 yscroll) {
  if (src >= 0xFE || ((src &= ~0x40) & 0x80) == 0) {
    // no y scroll
    memcpy(gfx_get_page(dst), gfx_get_page(src), PAGE_H * PAGE_W);
  } else {
    const u8 *srcpage = gfx_get_page(src & 3);
    u8 *dstpage = gfx_get_page(dst);
    if (srcpage != dstpage && yscroll >= -199 && yscroll <= 199) {
      if (yscroll < 0)
        memcpy(dstpage, srcpage - yscroll * PAGE_W, (PAGE_H + yscroll) * PAGE_W);
      else
        memcpy(dstpage + yscroll * PAGE_W, srcpage, (PAGE_H - yscroll) * PAGE_W);
    }
  }
}

void gfx_blit_bitmap(const u8 *ptr, const u32 size) {
  // decode; assumes amiga format
  register u8 *dst = gfx_page_work;
  register const u8 *src = ptr;
  for (int y = 0; y < PAGE_H; ++y) {
    for (int x = 0; x < PAGE_W; x += 8) {
      for (int b = 0; b < 8; ++b) {
        const int mask = 1 << (7 - b);
        u8 c = 0;
        if (src[0 * BITMAP_PLANE_SIZE] & mask) c |= 1 << 0;
        if (src[1 * BITMAP_PLANE_SIZE] & mask) c |= 1 << 1;
        if (src[2 * BITMAP_PLANE_SIZE] & mask) c |= 1 << 2;
        if (src[3 * BITMAP_PLANE_SIZE] & mask) c |= 1 << 3;
        *dst++ = c;
      }
      ++src;
    }
  }
}

static inline void gfx_draw_char(const u8 color, char ch, const s16 x, const s16 y) {
  const u8 *fchbase = gfx_font + ((ch - 0x20) << 3);
  const int ofs = x + y * PAGE_W;
  for (int j = 0; j < 8; ++j) {
    const u8 fch = fchbase[j];
    for (int i = 0; i < 8; ++i) {
      if (fch & (1 << (7 - i)))
        gfx_page_work[ofs + j * PAGE_W + i] = color;
    }
  }
}

void gfx_draw_string(const u8 col, s16 x, s16 y, const u16 strid) {
  const char *str = res_get_string(NULL, strid);
  if (!str) str = res_get_string(str_tab_demo, strid);
  if (!str) {
    printf("gfx_draw_string(%d, %d, %d, %d): unknown strid\n", (int)col, (int)x, (int)y, (int)strid);
    return;
  }

  const u16 startx = x;
  const int len = strlen(str);

  for (int i = 0; i < len; ++i) {
    if (str[i] == '\n' || str[i] == '\r') {
      y += 8;
      x = startx;
    } else {
      gfx_draw_char(col, str[i], x * 8, y);
      ++x;
    }
  }
}

void gfx_set_font(const u8 *data) {
  gfx_font = data;
}

#include <stdlib.h>
#include <stdio.h>
#include <psxgpu.h>

#include "types.h"
#include "game.h"
#include "gfx.h"
#include "res.h"
#include "util.h"
#include "tables.h"
#include "vm.h"
#include "pad.h"
#include "menu.h"

#define BMP_DELPHINE   0x47
#define BMP_ANOTHERW   0x53
#define BMP_OUTOFTHISW 0x13

#define MENU_START_X 16  // in 8-pixel columns
#define MENU_START_Y 120

#define TEXT_PALETTE 0x1B

static inline int wait_vblanks(int n, const int interrupt) {
  while (n--) {
    VSync(0);
    if (interrupt) {
      const u32 mask = pad_get_input() | pad_get_special_input();
      if (mask) return 1;
    }
  }
  return 0;
}

static inline int do_fade(const int from, const int to) {
  const int dir = (from > to) ? -1 : 1;
  int pal = from;
  while (pal != to) {
    gfx_set_next_palette(pal);
    if (wait_vblanks(2, 1)) return 1;
    gfx_update_display(0x00);
    pal += dir;
  }
  gfx_set_next_palette(pal);
  gfx_update_display(0x00);
  return 0;
}

static inline void menu_init(void) {
  // load the banks for the copy protection screen to get the bitmaps
  res_setup_part(PART_COPY_PROTECTION);
  // set font palette
  gfx_set_next_palette(TEXT_PALETTE);
}

static inline void menu_intro(void) {
  // do the fade-in for the logos and stuff
  res_load(BMP_DELPHINE);
  if (do_fade(0x17, 0x0F)) goto _interrupted;
  if (wait_vblanks(120, 1)) goto _interrupted; // wait ~1.5 sec
  if (do_fade(0x0F, 0x17)) goto _interrupted;
  res_load(gfx_get_current_mode() == MODE_PAL ? BMP_ANOTHERW : BMP_OUTOFTHISW);
  if (do_fade(0x0E, 0x09)) goto _interrupted;
  if (wait_vblanks(150, 1)) goto _interrupted;
  if (do_fade(0x09, 0x0E)) goto _interrupted;
  // draw credits
  gfx_set_next_palette(TEXT_PALETTE);
  gfx_fill_page(0x00, 0x00);
  gfx_draw_string(0x02, 0x12, 0x50, 0x181); // BY
  gfx_draw_string(0x03, 0x0F, 0x64, 0x182); // ERIC CHAHI
  wait_vblanks(1, 0);
  gfx_update_display(0x00);
  if (wait_vblanks(120, 1)) goto _interrupted;
  gfx_fill_page(0x00, 0x00);
  gfx_draw_string(0x04, 0x01, 0x50, 0x183); // MUSIC AND SOUND EFFECTS
  gfx_draw_string(0x05, 0x14, 0x64, 0x184); // (DE)
  gfx_draw_string(0x06, 0x0B, 0x78, 0x185); // JEAN-FRANCOIS FREITAS
  gfx_update_display(0x00);
  if (wait_vblanks(120, 1)) goto _interrupted;
  /*
  gfx_fill_page(0x00, 0x00);
  gfx_draw_string(0x04, 0x0E, 0x50, 0x186); // VERSION FOR IBM PC
  gfx_draw_string(0x05, 0x0E, 0x64, 0x187); // BY
  gfx_draw_string(0x06, 0x0E, 0x78, 0x188); // DANIEL MORAIS
  gfx_update_display(0x00);
  if (wait_vblanks(120, 1)) goto _interrupted;
  */
_interrupted:
  // clear screen
  gfx_set_next_palette(TEXT_PALETTE);
  gfx_fill_page(0x00, 0x00);
  gfx_update_display(0x00);
  gfx_set_work_page(0x00);
}

static inline int menu_choice(const int numstr, const u16 str[]) {
  int sel = 0;
  u32 old_mask = 0xFFFFFFFF; // hack to make the first frame always render

  while (1) {
    VSync(0);

    const u32 mask = pad_get_input() | pad_get_special_input();

    if ((mask & IN_DIR_DOWN) && !(old_mask & IN_DIR_DOWN)) {
      ++sel;
      if (sel >= numstr) sel = 0;
    } else if ((mask & IN_DIR_UP) && !(old_mask & IN_DIR_UP)) {
      --sel;
      if (sel < 0) sel = numstr - 1;
    }

    if (mask & (IN_ACTION | IN_PAUSE) && !(old_mask & (IN_ACTION | IN_PAUSE)))
      break;

    if (old_mask != mask) {
      int x = MENU_START_X;
      int y = MENU_START_Y;
      gfx_fill_page(0x00, 0x00);
      for (int i = 0; i < numstr; ++i) {
        gfx_draw_string(sel == i ? 0x06 : 0x02, x, y, str[i]);
        y += 8 + 2;
      }
      gfx_update_display(0x00);
    }

    old_mask = mask;
  }

  return sel;
}

static inline int menu_language(void) {
  const u16 strings[] = { 0x401, 0x402 }; // ENGLISH, FRENCH
  return menu_choice(2, strings);
}

static inline int menu_start_password(void) {
  const u16 strings[] = { 0x410, 0x411 }; // NEW GAME, PASSWORD
  return menu_choice(2, strings);
}

int menu_run(void) {
  menu_init();
  res_str_tab = menu_language() ? str_tab_fr : str_tab_en;
  if (res_have_password)
    menu_intro(); // demo already has an intro in itself
  const int part = (res_have_password && menu_start_password()) ?
    PART_PASSWORD : START_PART;
  // clear screen and all palettes
  gfx_set_next_palette(0x00);
  gfx_fill_page(0x00, 0x00);
  gfx_update_display(0x00);
  gfx_invalidate_palette();
  return part;
}

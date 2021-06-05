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

static inline void menu_intro(void) {
  // load the banks for the copy protection screen to get the bitmaps
  res_setup_part(PART_COPY_PROTECTION);
  // black
  gfx_set_palette(0x00);
  // do the fade-in for the logos and stuff
  res_load(BMP_DELPHINE);
  if (do_fade(0x17, 0x0F)) goto _interrupted;
  if (wait_vblanks(90, 1)) goto _interrupted; // wait ~1.5 sec
  if (do_fade(0x0F, 0x17)) goto _interrupted;
  res_load(gfx_get_current_mode() == MODE_PAL ? BMP_ANOTHERW : BMP_OUTOFTHISW);
  if (do_fade(0x0E, 0x09)) goto _interrupted;
  if (wait_vblanks(99, 1)) goto _interrupted;
  if (do_fade(0x09, 0x0E)) goto _interrupted;
_interrupted:
  // clear screen
  gfx_set_next_palette(0x1B);
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
  const u16 strings[] = { 0x401, 0x402 };
  return menu_choice(2, strings);
}

static inline int menu_start_password(void) {
  const u16 strings[] = { 0x410, 0x411 };
  return menu_choice(2, strings);
}

int menu_run(void) {
  menu_intro();
  res_str_tab = menu_language() ? str_tab_fr : str_tab_en;
  const int part = menu_start_password() ? PART_PASSWORD : PART_INTRO;
  // clear screen and palette
  gfx_set_next_palette(0x00);
  gfx_fill_page(0x00, 0x00);
  gfx_update_display(0x00);
  gfx_invalidate_palette();
  return part;
}

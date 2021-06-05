#include <string.h>
#include <stdio.h>
#include <psxgpu.h>

#include "types.h"
#include "vm.h"
#include "util.h"
#include "gfx.h"
#include "snd.h"
#include "music.h"
#include "pad.h"
#include "res.h"
#include "tables.h"
#include "game.h"

#define VM_NUM_VARS    0x100
#define VM_STACK_DEPTH 0x40
#define VM_NUM_TASKS   0x40
#define VM_NUM_OPCODES 27

typedef void (* op_func_t)(void);

static struct {
  u8 halt;
  s16 vars[VM_NUM_VARS];
  u16 callstack[VM_STACK_DEPTH];
  u16 script_pos[2][VM_NUM_TASKS];
  u8 script_paused[2][VM_NUM_TASKS];
  u8 *pc;
  u8 sp;
} vm;

static u32 time_now;
static u32 time_start;

static inline u8 vm_fetch_u8(void) {
  return *(vm.pc++);
}

static inline u16 vm_fetch_u16(void) {
  const u8 *b = vm.pc;
  vm.pc += 2;
  return (b[0] << 8) | b[1];
}

static void op_mov_const(void) {
  const u8 i = vm_fetch_u8();
  const s16 x = vm_fetch_u16();
  vm.vars[i] = x;
}

static void op_mov(void) {
  const u8 i = vm_fetch_u8();
  const u8 j = vm_fetch_u8();
  vm.vars[i] = vm.vars[j];
}

static void op_add(void) {
  const u8 i = vm_fetch_u8();
  const u8 j = vm_fetch_u8();
  vm.vars[i] += vm.vars[j];
}

static void op_add_const(void) {
  const u8 i = vm_fetch_u8();
  const s16 x = vm_fetch_u16();
  vm.vars[i] += x;
}

static void op_call(void) {
  const u16 ofs = vm_fetch_u16();
  vm.callstack[vm.sp++] = vm.pc - res_seg_code;
  vm.pc = res_seg_code + ofs;
}

static void op_ret(void) {
  vm.pc = res_seg_code + vm.callstack[--vm.sp];
}

static void op_break(void) {
  vm.halt = 1;
}

static void op_jmp(void) {
  const u16 ofs = vm_fetch_u16();
  vm.pc = res_seg_code + ofs;
}

static void op_set_script_slot(void) {
  const u8 i = vm_fetch_u8();
  const u16 val = vm_fetch_u16();
  vm.script_pos[1][i] = val;
}

static void op_jnz(void) {
  const u8 i = vm_fetch_u8();
  --vm.vars[i];
  if (vm.vars[i])
    op_jmp();
  else
    vm_fetch_u16();
}

static void op_condjmp(void) {
  const u8 op = vm_fetch_u8();
  const s16 b = vm.vars[vm_fetch_u8()];
  const u8 c = vm_fetch_u8();
  s16 a;
  if (op & 0x80)
    a = vm.vars[c];
  else if (op & 0x40)
    a = c * 256 + vm_fetch_u8();
  else
    a = c;
  int expr = 0;
  switch (op & 7) {
    case 0: expr = (b == a); break; // jz
    case 1: expr = (b != a); break; // jnz
    case 2: expr = (b >  a); break; // jg
    case 3: expr = (b >= a); break; // jge
    case 4: expr = (b <  a); break; // jl
    case 5: expr = (b <= a); break; // jle
    default: break;
  }
  if (expr)
    op_jmp();
  else
    vm_fetch_u16();
}

static void op_set_palette(void) {
  const u16 p = vm_fetch_u16() >> 8;
  gfx_set_next_palette(p);
}

static void op_reset_script(void) {
  const u8 j = vm_fetch_u8();
  const u8 i = vm_fetch_u8();
  register s8 n = (i & 0x3F) - j;
  if (n < 0) {
    printf("op_reset_script(): n=%d < 0\n", n);
    return;
  }
  ++n;
  const u8 a = vm_fetch_u8();
  if (a == 2) {
    register u16 *p = &vm.script_pos[1][j];
    while (n--) *p++ = 0xFFFE;
  } else if (a < 2) {
    register u8 *p = &vm.script_paused[1][j];
    while (n--) *p++ = a;
  }
}

static void op_select_page(void) {
  const u8 p = vm_fetch_u8();
  gfx_set_work_page(p);
}

static void op_fill_page(void) {
  const u8 screen = vm_fetch_u8();
  const u8 color = vm_fetch_u8();
  gfx_fill_page(screen, color);
}

static void op_copy_page(void) {
  const u8 src = vm_fetch_u8();
  const u8 dst = vm_fetch_u8();
  gfx_copy_page(src, dst, vm.vars[VAR_SCROLL_Y]);
}

static void op_update_display(void) {
  static u32 tstamp = 0;

  const u8 page = vm_fetch_u8();

  vm_handle_special_input(pad_get_special_input());

  if (res_cur_part == 0x3E80 && vm.vars[0x67] == 1)
    vm.vars[0xDC] = 0x21;

  const s32 delay = VSync(-1) - tstamp;
  s32 pause = vm.vars[VAR_PAUSE_SLICES] - delay;
  for (; pause > 0; --pause) VSync(0);
  tstamp = VSync(-1);

  vm.vars[0xF7] = 0;

  gfx_update_display(page);
}

static void op_halt(void) {
  vm.pc = res_seg_code + 0xFFFF;
  vm.halt = 1;
}

static void op_draw_string(void) {
  const u16 strid = vm_fetch_u16();
  const u8 x = vm_fetch_u8();
  const u8 y = vm_fetch_u8();
  const u8 col = vm_fetch_u8();
  gfx_draw_string(col, x, y, strid);
}

static void op_sub(void) {
  const u8 i = vm_fetch_u8();
  const u8 j = vm_fetch_u8();
  vm.vars[i] -= vm.vars[j];
}

static void op_and(void) {
  const u8 i = vm_fetch_u8();
  const u16 x = vm_fetch_u16();
  vm.vars[i] = (u16)vm.vars[i] & x;
}

static void op_or(void) {
  const u8 i = vm_fetch_u8();
  const u16 x = vm_fetch_u16();
  vm.vars[i] = (u16)vm.vars[i] | x;
}

static void op_shl(void) {
  const u8 i = vm_fetch_u8();
  const u16 x = vm_fetch_u16();
  vm.vars[i] = (u16)vm.vars[i] << x;
}

static void op_shr(void) {
  const u8 i = vm_fetch_u8();
  const u16 x = vm_fetch_u16();
  vm.vars[i] = (u16)vm.vars[i] >> x;
}

static void op_update_memlist(void) {
  const u16 num = vm_fetch_u16();
  if (num == 0) {
    mus_stop();
    snd_stop_all();
    res_invalidate_res();
  } else {
    res_load(num);
  }
}

static void op_play_sound(void) {
  const u16 res = vm_fetch_u16();
  const u8 freq = vm_fetch_u8();
  u8 vol = vm_fetch_u8();
  const u8 channel = vm_fetch_u8();

  if (vol > 63) {
    vol = 63;
  } else if (vol == 0) {
    snd_stop_sound(channel);
    return;
  }

  const mementry_t *me = res_get_entry(res);
  if (me && me->status == RS_LOADED) {
    ASSERT(freq < 40);
    snd_play_sound(channel & 3, me->bufptr, freq_tab[freq], vol);
  }
}

static void op_play_music(void) {
  const u16 res = vm_fetch_u16();
  const u16 delay = vm_fetch_u16();
  const u8 pos = vm_fetch_u8();

  if (res != 0) {
    mus_load(res, delay, pos);
    mus_start();
  } else if (delay != 0) {
    mus_set_delay(delay);
  } else {
    mus_stop();
  }
}

static op_func_t vm_op_table[] = {
  /* 0x00 */
  &op_mov_const,
  &op_mov,
  &op_add,
  &op_add_const,
  /* 0x04 */
  &op_call,
  &op_ret,
  &op_break,
  &op_jmp,
  /* 0x08 */
  &op_set_script_slot,
  &op_jnz,
  &op_condjmp,
  &op_set_palette,
  /* 0x0C */
  &op_reset_script,
  &op_select_page,
  &op_fill_page,
  &op_copy_page,
  /* 0x10 */
  &op_update_display,
  &op_halt,
  &op_draw_string,
  &op_sub,
  /* 0x14 */
  &op_and,
  &op_or,
  &op_shl,
  &op_shr,
  /* 0x18 */
  &op_play_sound,
  &op_update_memlist,
  &op_play_music
};

int vm_init(void) {
  memset(vm.vars, 0, sizeof(vm.vars));
  vm.vars[0xE4] = 0x14; // copy protection checks this
  // 0x01 == "Another World", 0x81 == "Out of This World"
  vm.vars[0x54] = gfx_get_current_mode() == MODE_PAL ? 0x01 : 0x81;
  vm.vars[VAR_RANDOM_SEED] = 0x1337;
#ifndef KEEP_COPY_PROTECTION
  // if the game was built to start at the intro, set all the copy protection related shit
  vm.vars[0xBC] = 0x10;
  vm.vars[0xC6] = 0x80;
  vm.vars[0xDC] = 0x21;
  vm.vars[0xF2] = 4000; // this is for DOS, Amiga wants 6000
#endif
}

void vm_restart_at(const u16 part_id, const u16 pos) {
  mus_stop();
  snd_stop_all();
  res_setup_part(part_id);
  memset(vm.script_pos, 0xFF, sizeof(vm.script_pos));
  memset(vm.script_paused, 0, sizeof(vm.script_paused));
  vm.script_pos[0][0] = 0;
  if (pos >= 0) vm.vars[0] = pos;
  time_now = time_start = 0; // get_timestamp()
}

void vm_setup_tasks(void) {
  if (res_next_part) {
    printf("vm_setup_tasks(): transitioning to part %05u\n", res_next_part);
    vm_restart_at(res_next_part, 0);
    res_next_part = 0;
  }
  for (int i = 0; i < VM_NUM_TASKS; ++i) {
    vm.script_paused[0][i] = vm.script_paused[1][i];
    const u16 pos = vm.script_pos[1][i];
    if (pos != 0xFFFF) {
      vm.script_pos[0][i] = (pos == 0xFFFE) ? 0xFFFF : pos;
      vm.script_pos[1][i] = 0xFFFF;
    }
  }
}

static void vm_run_task(void) {
  while (!vm.halt) {
    const u8 op = vm_fetch_u8();
    if (op & 0x80) {
      res_vidseg_idx = 0;
      const u16 ofs = ((op << 8) | vm_fetch_u8()) << 1;
      s16 x = vm_fetch_u8();
      s16 y = vm_fetch_u8();
      const s16 h = y - 199;
      if (h > 0) {
        y = 199;
        x += h;
      }
      gfx_set_databuf(res_seg_video[0], ofs);
      gfx_draw_shape(0xFF, 0x40, x, y);
    } else if (op & 0x40) {
      res_vidseg_idx = 0;
      const u16 ofs = vm_fetch_u16() << 1;
      s16 x = vm_fetch_u8();
      if ((op & 0x20) == 0) {
        if ((op & 0x10) == 0)
          x = (x << 8) | vm_fetch_u8();
        else
          x = vm.vars[x];
      } else if (op & 0x10) {
          x += 0x100;
      }
      s16 y = vm_fetch_u8();
      if ((op & 8) == 0) {
        if ((op & 4) == 0)
          y = (y << 8) | vm_fetch_u8();
        else
          y = vm.vars[y];
      }
      u16 zoom = 0x40;
      if ((op & 2) == 0) {
        if (op & 1)
          zoom = vm.vars[vm_fetch_u8()];
      } else if (op & 1) {
        res_vidseg_idx = 1;
      } else {
        zoom = vm_fetch_u8();
      }
      gfx_set_databuf(res_seg_video[res_vidseg_idx], ofs);
      gfx_draw_shape(0xFF, zoom, x, y);
    } else if (op < VM_NUM_OPCODES) {
      vm_op_table[op]();
    } else {
      printf("vm_run_task(pc=%p): invalid opcode %02x\n", vm.pc, op);
    }
  }
}

void vm_run(void) {
  for (int i = 0; i < VM_NUM_TASKS; ++i) {
    if (vm.script_paused[0][i] == 0) {
      const u16 pos = vm.script_pos[0][i];
      if (pos != 0xFFFF) {
        vm.pc = res_seg_code + pos;
        vm.sp = 0;
        vm.halt = 0;
        vm_run_task();
        vm.script_pos[0][i] = vm.pc - res_seg_code;
      }
    }
  }
}

void vm_set_var(const u8 i, const s16 val) {
  vm.vars[i] = val;
}

s16 vm_get_var(const u8 i) {
  return vm.vars[i];
}

void vm_handle_special_input(u32 mask) {
  if (mask & IN_PAUSE) {
    if (res_cur_part != PART_COPY_PROTECTION && res_cur_part != PART_INTRO) {
      mask &= ~IN_PAUSE;
      do {
        VSync(0);
        mask = pad_get_special_input();
      } while (!(mask & IN_PAUSE));
    }
  }

  if (mask & IN_PASSWORD) {
    if (res_cur_part != PART_COPY_PROTECTION && res_cur_part != PART_PASSWORD && res_have_password)
      res_next_part = PART_PASSWORD;
  }
}

void vm_update_input(u32 mask) {
  s16 lr = 0;
  s16 m = 0;
  s16 ud = 0;
  s16 jd = 0;

  if (mask & IN_DIR_RIGHT)
    lr =  1, m |= 1;
  if (mask & IN_DIR_LEFT)
    lr = -1, m |= 2;
  if (mask & IN_DIR_DOWN)
    ud = jd =  1, m |= 4;
  if (mask & (IN_DIR_UP | IN_JUMP))
    ud = jd = -1, m |= 8;

  vm.vars[VAR_HERO_POS_UP_DOWN] = ud;
  vm.vars[VAR_HERO_POS_JUMP_DOWN] = jd;
  vm.vars[VAR_HERO_POS_LEFT_RIGHT] = lr;
  vm.vars[VAR_HERO_POS_MASK] = m;

  s16 action = 0;
  if (mask & (IN_ACTION))
    action = 1, m |= 0x80;

  vm.vars[VAR_HERO_ACTION] = action;
  vm.vars[VAR_HERO_ACTION_POS_MASK] = m;
}

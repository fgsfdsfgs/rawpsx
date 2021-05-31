#pragma once

#include "types.h"

enum {
  VAR_RANDOM_SEED          = 0x3C,
  VAR_LAST_KEYCHAR         = 0xDA,
  VAR_HERO_POS_UP_DOWN     = 0xE5,
  VAR_MUS_MARK             = 0xF4,
  VAR_SCROLL_Y             = 0xF9,
  VAR_HERO_ACTION          = 0xFA,
  VAR_HERO_POS_JUMP_DOWN   = 0xFB,
  VAR_HERO_POS_LEFT_RIGHT  = 0xFC,
  VAR_HERO_POS_MASK        = 0xFD,
  VAR_HERO_ACTION_POS_MASK = 0xFE,
  VAR_PAUSE_SLICES         = 0xFF
};

int vm_init(void);
void vm_setup_scripts(void);
void vm_run(void);
void vm_set_var(const u8 i, const s16 val);
s16 vm_get_var(const u8 i);
void vm_restart_at(const u16 part_id, const u16 pos);
void vm_setup_tasks(void);
void vm_run(void);
void vm_update_input(const u32 mask);

#include <psxpad.h>
#include <psxapi.h>
#include <stdio.h>

#include "types.h"
#include "gfx.h"
#include "res.h"
#include "vm.h"
#include "util.h"
#include "main.h"

static PADTYPE *pad;
static u8 pad_buf[2][34];

static inline void pad_init(void) {
  InitPAD(pad_buf[0], sizeof(pad_buf[0]), pad_buf[1], sizeof(pad_buf[1]));
  StartPAD();
  ChangeClearPAD(0);
  pad = (PADTYPE *)pad_buf[0];
}

static inline u32 pad_get_input(void) {
  register u32 mask = 0;
  if (pad->btn & PAD_UP)     mask |= IN_DIR_UP;
  if (pad->btn & PAD_DOWN)   mask |= IN_DIR_DOWN;
  if (pad->btn & PAD_LEFT)   mask |= IN_DIR_LEFT;
  if (pad->btn & PAD_RIGHT)  mask |= IN_DIR_RIGHT;
  if (pad->btn & PAD_CIRCLE) mask |= IN_JUMP;
  if (pad->btn & PAD_CROSS)  mask |= IN_ACTION;
  if (pad->btn & PAD_START)  mask |= IN_PAUSE;
  return mask;
}

int main(int argc, const char *argv[]) {
  gfx_init();
  res_init();
  vm_init();
  // snd_init();
  pad_init();

  vm_restart_at(START_PART, 0);

  while (1) {
    vm_setup_tasks();
    vm_update_input(pad_get_input());
    vm_run();
    // snd_update();
  }

  return 0;
}

#include <psxpad.h>
#include <psxapi.h>

#include "types.h"
#include "pad.h"

static PADTYPE *pad;
static u8 pad_buf[2][34];

void pad_init(void) {
  InitPAD(pad_buf[0], sizeof(pad_buf[0]), pad_buf[1], sizeof(pad_buf[1]));
  StartPAD();
  ChangeClearPAD(0);
  pad = (PADTYPE *)pad_buf[0];
}

u32 pad_get_input(void) {
  static u32 old_mask = 0;
  register u32 mask = 0;
  if (!(pad->btn & PAD_UP))     mask |= IN_DIR_UP;
  if (!(pad->btn & PAD_DOWN))   mask |= IN_DIR_DOWN;
  if (!(pad->btn & PAD_LEFT))   mask |= IN_DIR_LEFT;
  if (!(pad->btn & PAD_RIGHT))  mask |= IN_DIR_RIGHT;
  if (!(pad->btn & PAD_CIRCLE)) mask |= IN_JUMP;
  if (!(pad->btn & PAD_CROSS))  mask |= IN_ACTION;
  // clear pause button after one press
  if (!(pad->btn & PAD_START) && !(old_mask & IN_PAUSE))
    mask |= IN_PAUSE;
  old_mask = mask;
  return mask;
}

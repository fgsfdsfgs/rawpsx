#include <stdio.h>

#include "types.h"
#include "gfx.h"
#include "snd.h"
#include "music.h"
#include "pad.h"
#include "res.h"
#include "vm.h"
#include "util.h"
#include "game.h"

int main(int argc, const char *argv[]) {
  gfx_init();
  res_init();
  snd_init();
  mus_init();
  pad_init();
  vm_init();

  vm_restart_at(START_PART, 0);

  while (1) {
    vm_setup_tasks();
    vm_update_input(pad_get_input());
    vm_run();
    snd_update();
    mus_update();
  }

  return 0;
}

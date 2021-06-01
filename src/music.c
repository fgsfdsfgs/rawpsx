#include <stdio.h>
#include <string.h>
#include <psxapi.h>
#include <psxetc.h>

#include "types.h"
#include "res.h"
#include "snd.h"
#include "util.h"
#include "tables.h"
#include "game.h"
#include "vm.h"
#include "music.h"

#define NUM_INST 15
#define NUM_CH 4
#define MAX_ORDER 0x80
#define PAT_SIZE 1024

#define GPU_STATUS  ((volatile u32 *)0x1F801814)
#define BIOS_REGION ((const char   *)0xBFC7FF52)

typedef struct {
  const u8 *data;
  u16 vol;
} mus_inst_t;

typedef struct {
  const u8 *data;
  u16 pos;
  u8 cur_order;
  u8 num_order;
  u8 order_tab[MAX_ORDER];
  mus_inst_t inst[NUM_INST];
} mus_module_t;

static mus_module_t mus_mod;
static u32 mus_delay = 0;
static u32 mus_base_clock = 0;

static volatile int mus_playing = 0;
static volatile int mus_request_stop = 0;

static void mus_callback(void);

// https://github.com/grumpycoders/pcsx-redux/blob/main/src/mips/modplayer/modplayer.c:195
static inline u32 mus_get_base_clock(void) {
  static const u32 clocks[4] = {
    39336, // !mode && !bios => 262.5 * 125 * 59.940 / 50 or 263 * 125 * 59.826 / 50
    38977, // !mode &&  bios => 262.5 * 125 * 59.393 / 50 or 263 * 125 * 59.280 / 50
    39422, //  mode && !bios => 312.5 * 125 * 50.460 / 50 or 314 * 125 * 50.219 / 50
    39062, //  mode &&  bios => 312.5 * 125 * 50.000 / 50 or 314 * 125 * 49.761 / 50
  };
  const u32 gpu_status = *GPU_STATUS;
  const u32 is_pal_bios = (*BIOS_REGION == 'E');
  const u32 is_pal_mode = ((gpu_status & 0x00100000) != 0);
  const u32 clk = clocks[(is_pal_mode << 1) | is_pal_bios];
  // rescale to ~20fps
  return clk / 3;
}

static inline u32 mus_get_delay_ticks(const u32 delay) {
  // get our hblank clock ticks from meme amiga ticks and hope it doesn't overflow
  const u32 ms = delay * 60 / 7050;
  const u32 bpm = (1000 / ms);
  return mus_base_clock / bpm;
}

void mus_init(void) {
  // detect clocks per second
  mus_base_clock = mus_get_base_clock();
  printf("mus_init(): base BPM clock: %u\n", mus_base_clock);
  // set up timer
  EnterCriticalSection();
  SetRCnt(RCntCNT1, 0xFFFF, RCntMdINTR); // set it to max for now
  InterruptCallback(5, mus_callback); // IRQ5 is RCNT1
  ExitCriticalSection();
}

static inline void mus_load_instruments(const u8 *p) {
  for (int i = 0; i < NUM_INST; ++i) {
    mus_inst_t *inst = mus_mod.inst + i;
    const u16 resid = read16be(p); p += 2;
    if (resid != 0) {
      inst->vol = read16be(p);
      const mementry_t *me = res_get_entry(resid);
      if (me && me->status == RS_LOADED && me->type == RT_SOUND)
        inst->data = me->bufptr;
      else
        printf("mus_load_instruments(): %04x is not a sound resource\n", resid);
    }
    p += 2;
  }
}

void mus_load(const u16 resid, const u16 delay, const u8 pos) {
  const mementry_t *me = res_get_entry(resid);
  ASSERT(me != NULL);

  if (me->status != RS_LOADED || me->type != RT_MUSIC) {
    printf("mus_load(): %04x is not a music resource\n", resid);
    return;
  }

  memset(&mus_mod, 0, sizeof(mus_mod));

  mus_mod.cur_order = pos;
  mus_mod.num_order = read16be(me->bufptr + 0x3E);
  memcpy(mus_mod.order_tab, me->bufptr + 0x40, sizeof(mus_mod.order_tab));

  if (delay == 0)
    mus_delay = read16be(me->bufptr);
  else
    mus_delay = delay;

  mus_delay = mus_get_delay_ticks(mus_delay);

  mus_mod.data = me->bufptr + 0xC0;

  printf("mus_load(%04x, %04x, %02x): loading module, delay=%u\n", resid, delay, pos, mus_delay);

  mus_load_instruments(me->bufptr + 0x02);
}

void mus_start(void) {
  mus_mod.pos = 0;
  mus_playing = 1;
  mus_request_stop = 0;
  EnterCriticalSection();
  SetRCnt(RCntCNT1, mus_delay, RCntMdINTR);
  StartRCnt(RCntCNT1);
  ChangeClearRCnt(1, 0);
  ExitCriticalSection();
}

void mus_set_delay(const u16 delay) {
  mus_delay = mus_get_delay_ticks(delay);
  // restart the timer
  EnterCriticalSection();
  StopRCnt(RCntCNT1);
  SetRCnt(RCntCNT1, mus_delay, RCntMdINTR);
  StartRCnt(RCntCNT1);
  ChangeClearRCnt(1, 0);
  ExitCriticalSection();
}

void mus_stop(void) {
  mus_playing = 0;
  mus_request_stop = 0;
  EnterCriticalSection();
  StopRCnt(RCntCNT1);
  ExitCriticalSection();
}

void mus_update(void) {
  if (mus_request_stop)
    mus_stop();
}

static inline void mus_handle_pattern(const u8 ch, const u8 *data) {
  const u16 note1 = read16be(data + 0);
  const u16 note2 = read16be(data + 2);
  const u8 *sndptr = NULL;
  u16 sndvol = 0;

  if (note1 == 0xFFFD) {
    vm_set_var(VAR_MUS_MARK, note2);
    return;
  }

  const u16 inst = (note2 & 0xF000) >> 12;
  if (inst != 0) {
    sndptr = mus_mod.inst[inst - 1].data;
    if (sndptr) {
      sndvol = mus_mod.inst[inst - 1].vol;
      const u8 effect = (note2 & 0x0F00) >> 8;
      if (effect == 6) {
        // volume down
        sndvol -= (note2 & 0xFF);
        if (sndvol < 0) sndvol = 0;
      } else if (effect == 5) {
        // volume up
        sndvol += (note2 & 0xFF);
        if (sndvol > 0x3F) sndvol = 0x3F;
      }
      snd_set_sound_vol(ch, sndvol);
    }
  }

  if (note1 == 0xFFFE) {
    snd_stop_sound(ch);
  } else if (note1 && sndptr) {
    const u16 sndfreq = 7159092 / (note1 << 1);
    snd_play_sound(ch, sndptr, sndfreq, sndvol);
  }
}

// interrupt callback
static void mus_callback(void)  {
  if (!mus_playing || mus_request_stop) return;

  u8 order = mus_mod.order_tab[mus_mod.cur_order];
  const u8 *patdata = mus_mod.data + mus_mod.pos + ((u32)order * PAT_SIZE);

  for (u8 ch = 0; ch < NUM_CH; ++ch) {
    mus_handle_pattern(ch, patdata);
    patdata += 4;
  }

  mus_mod.pos += 4 * NUM_CH;

  if (mus_mod.pos >= PAT_SIZE) {
    mus_mod.pos = 0;
    order = mus_mod.cur_order + 1;
    if (order == mus_mod.num_order)
      mus_request_stop = 1; // game loop will fix the rest, since we can't fuck with events from here
    else
      mus_mod.cur_order = order;
  }
}

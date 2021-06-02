#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <psxapi.h>
#include <psxspu.h>

#include "types.h"
#include "util.h"
#include "adpcm.h"
#include "snd.h"

#define SPU_MEM_MAX   0x80000
#define SPU_MEM_START 0x1100

#define SPU_VOL_MAX +0x3FFF
#define SPU_VOL_MIN -0x4000
#define SPU_VOL_RANGE (SPU_VOL_MAX - SPU_VOL_MIN)

#define CH_SOUND_BASE 4
#define CH_MUSIC_BASE 0
#define CH_SOUND_COUNT 4
#define CH_MUSIC_COUNT 4

#define VAG_DATA_OFFSET 48
#define PCM_DATA_OFFSET 8

#define SND_CVTBUF_SIZE (64 * 1024)

#define SPU_VOICE_BASE ((volatile u16 *)(0x1F801C00))
#define SPU_KEY_ON_LO  ((volatile u16 *)(0x1F801D88))
#define SPU_KEY_ON_HI  ((volatile u16 *)(0x1F801D8A))
#define SPU_KEY_OFF_LO ((volatile u16 *)(0x1F801D8C))
#define SPU_KEY_OFF_HI ((volatile u16 *)(0x1F801D8E))

struct spu_voice {
  volatile u16 vol_left;
  volatile u16 vol_right;
  volatile u16 sample_rate;
  volatile u16 sample_startaddr;
  volatile u16 attack_decay;
  volatile u16 sustain_release;
  volatile u16 vol_current;
  volatile u16 sample_repeataddr;
};
#define SPU_VOICE(x) (((volatile struct spu_voice *)SPU_VOICE_BASE) + (x))

struct sound {
  const u8 *addr;
  s32 spuaddr;
  s32 size;
};

static u8 snd_cvtbuf[SND_CVTBUF_SIZE] __attribute__((aligned(64)));

static sound_t snd_cache[MAX_SOUNDS];
static u32 snd_cache_num = 0;
static s32 snd_spu_ptr = 0; // current SPU mem address

static u32 snd_key_mask = 0;

static inline u16 freq2pitch(const u32 hz) {
  return (hz << 12) / 44100;
}

static inline s32 spu_alloc(s32 size) {
  // SPU likes 8-byte alignment
  size = ALIGN(size, 8);
  ASSERT(snd_spu_ptr + size <= SPU_MEM_MAX);
  const s32 ptr = snd_spu_ptr;
  snd_spu_ptr += size;
  return ptr;
}

static inline void spu_key_on(const u32 mask) {
  *SPU_KEY_ON_LO = mask;
  *SPU_KEY_ON_HI = mask >> 16;
}

static inline void spu_key_off(const u32 mask) {
  *SPU_KEY_OFF_LO = mask;
  *SPU_KEY_OFF_HI = mask >> 16;
}

static inline void spu_clear_voice(const u32 v) {
  SPU_VOICE(v)->vol_left = 0;
  SPU_VOICE(v)->vol_right = 0;
  SPU_VOICE(v)->sample_rate = 0;
  SPU_VOICE(v)->sample_startaddr = 0;
  SPU_VOICE(v)->sample_repeataddr = 0;
  SPU_VOICE(v)->attack_decay = 0x000F;
  SPU_VOICE(v)->sustain_release = 0x0000;
  SPU_VOICE(v)->vol_current = 0;
}

// unfortunately the psn00bsdk function for this is bugged:
// it checks against 0x1000..0xffff instead of 0x1000..0x7ffff
// fortunately, the address is stored in a global variable
// unfortunately, reading it from C requires GP-relative addressing
// so we have to implement the function in assembly (see spu.s)
extern u32 spu_set_transfer_addr(const u32 addr);

void snd_init(void) {
  SpuInit();
  snd_clear_cache();
  snd_stop_all();
  for (u32 v = 0; v < 24; ++v)
    spu_clear_voice(v);
}

void snd_clear_cache(void) {
  for (int i = 0; i < MAX_SOUNDS; ++i) {
    // mark as unloaded
    snd_cache[i].spuaddr = -1;
    snd_cache[i].addr = NULL;
    snd_cache[i].size = 0;
  }
  // reset allocator
  snd_spu_ptr = SPU_MEM_START;
  snd_cache_num = 0;
}

static inline sound_t *snd_cache_find(const u8 *ptr) {
  for (int i = 0; i < MAX_SOUNDS; ++i)
    if (snd_cache[i].addr == ptr)
      return snd_cache + i;
  return NULL;
}

static u16 snd_convert_pcm(u8 *out, u32 outsize, const u8 *in, u32 insize, int loop0, int loop1) {
  const s32 adpcm_size = adpcm_pack_mono_s8(out, outsize, (const s8 *)in, insize, loop0, loop1);
  ASSERT(adpcm_size >= 0);
  return adpcm_size;
}

sound_t *snd_cache_sound(const u8 *data, u16 size, const int type) {
  sound_t *snd = snd_cache_find(data);
  if (snd) {
    printf("snd_cache_sound(%p): already cached as %d\n", data, snd - snd_cache);
    return snd;
  }

  u8 *cvtbuf = NULL; // in case we need to convert the sound

  snd = &snd_cache[snd_cache_num++];
  snd->addr = data;
  if (size == 0) {
    // NULL sound
    snd->spuaddr = 0;
    snd->size = 0;
    return snd;
  } else if (type == SND_TYPE_VAG) {
    // sound is already in VAG format, just load it in
    snd->spuaddr = spu_alloc(size);
    snd->size = size - VAG_DATA_OFFSET; // skip header
    data += VAG_DATA_OFFSET; // skip header
  } else {
    int loopstart = -1; // loop start position, in PCM samples
    int loopend = -1;   // loop end position
    // there might be a header
    if (type == SND_TYPE_PCM_WITH_HEADER) {
      const s32 lstart = read16be(data) << 1;
      const s32 lsize  = read16be(data + 2) << 1;
      if (lsize) {
        // there's a loop point; the ADPCM converter will take care of that
        loopstart = lstart;
        loopend = (s32)lstart + lsize;
      }
      size = lstart + lsize;
      data += PCM_DATA_OFFSET; // skip header
    }
    // need to convert it, output will be at most the same size
    // SPU transfers are done in blocks of 64, so we'll just align all sizes to that
    const u32 alignedsize = ALIGN(size, 64);
    ASSERT(alignedsize <= sizeof(snd_cvtbuf));
    snd->size = snd_convert_pcm(snd_cvtbuf, sizeof(snd_cvtbuf), data, size, loopstart, loopend);
    snd->size = ALIGN(snd->size, 64);
    snd->spuaddr = spu_alloc(snd->size);
    data = snd_cvtbuf;
  }

  SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
  spu_set_transfer_addr(snd->spuaddr);
  SpuWrite((void *)data, snd->size);
  SpuWait(); // wait for transfer to complete

  return snd;
}

void snd_play_sound(const u8 ch, const u8 *data, const u16 freq, const u8 vol) {
  const sound_t *snd = snd_cache_find(data);
  if (!snd) {
    // FIXME: this will explode if reached from the music timer handler
    printf("snd_play_sound(%p): unknown sound\n", data);
    return;
  }
  const u32 chmask = SPU_VOICECH((u32)ch);
  spu_key_off(chmask);
  if (snd->size && snd->spuaddr >= 0) {
    const s16 vvol = (s16)vol << 8;
    SPU_VOICE(ch)->vol_left = vvol;
    SPU_VOICE(ch)->vol_right = vvol;
    SPU_VOICE(ch)->sample_rate = freq2pitch(freq);
    SPU_VOICE(ch)->sample_repeataddr = 0;
    SPU_VOICE(ch)->sample_startaddr = ((u32)snd->spuaddr >> 3);
    snd_key_mask |= chmask;
    SpuWait();
    spu_key_on(chmask);
  }
}

void snd_stop_sound(const u8 ch) {
  const u32 chmask = SPU_VOICECH((u32)ch);
  snd_key_mask &= chmask;
  snd_set_sound_vol(ch, 0);
  SpuWait();
  spu_key_off(chmask);
}

void snd_stop_all(void) {
  SpuWait();
  spu_key_off(0xFFFFFF); // kill all voices
}

void snd_set_sound_vol(const u8 ch, const u8 vol) {
  const s16 vvol = (s16)vol << 8;
  SPU_VOICE(ch)->vol_left = vvol;
  SPU_VOICE(ch)->vol_right = vvol;
}

void snd_update(void) {

}

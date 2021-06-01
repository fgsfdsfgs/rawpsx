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

#define SPU_VOICE_BASE ((volatile u16 *)(0x1F801C00))
#define SPU_KEY_ON_LO  ((volatile u16 *)(0x1F801D88))
#define SPU_KEY_ON_HI  ((volatile u16 *)(0x1F801D8A))
#define SPU_KEY_OFF_LO ((volatile u16 *)(0x1F801D8C))
#define SPU_KEY_OFF_HI ((volatile u16 *)(0x1F801D8E))

#define SPU_VOL_MAX +0x3FFF
#define SPU_VOL_MIN -0x4000
#define SPU_VOL_RANGE (SPU_VOL_MAX - SPU_VOL_MIN)

#define CH_SOUND_BASE 4
#define CH_MUSIC_BASE 0
#define CH_SOUND_COUNT 4
#define CH_MUSIC_COUNT 4

#define VAG_DATA_OFFSET 48

#define SND_BASE_FREQ 11025

struct spu_voice {
  u16 vol_left;
  u16 vol_right;
  u16 sample_rate;
  u16 sample_startaddr;
  u16 attack_decay;
  u16 sustain_release;
  u16 vol_current;
  u16 sample_repeataddr;
};
#define SPU_VOICE(x) (((volatile struct spu_voice *)SPU_VOICE_BASE) + (x))

struct sound {
  const u8 *addr;
  s32 spuaddr;
  u16 size;
};

static sound_t snd_cache[MAX_SOUNDS];
static u32 snd_cache_num = 0;
static s32 snd_spu_ptr = 0; // current SPU mem address

static u32 snd_key_mask = 0;

static inline u16 freq2pitch(const u32 hz) {
  return (hz << 12) / 44100;
}

static inline s32 spu_alloc(const s32 size) {
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

static u16 snd_convert_pcm(u8 *out, const u8 *in, const u16 size, const int loop) {
  const s32 adpcm_size = adpcm_pack_mono_s8(out, size, (const s8 *)in, size, loop);
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
    u16 loop = 0;
    // there might be a header
    if (type == SND_TYPE_PCM_WITH_HEADER) {
      size = read16be(data) << 1;
      loop = (read16be(data + 2) << 1) != 0;
      data += 8; // skip header
    }
    // need to convert it, output will be at most the same size
    cvtbuf = malloc(size);
    ASSERT(cvtbuf != NULL);
    snd->size = snd_convert_pcm(cvtbuf, data, size, loop);
    snd->spuaddr = spu_alloc(snd->size);
  }

  SpuSetTransferStartAddr(snd->spuaddr);
  if (cvtbuf) {
    // we've converted the sound, write the result and free it
    SpuWrite((void *)cvtbuf, snd->size);
    SpuWait(); // wait for transfer to end before freeing the buffer
    free(cvtbuf);
  } else {
    // sound was already ADPCM
    SpuWrite((void *)data, snd->size);
    SpuWait(); // wait for transfer to end
  }

  return snd;
}

void snd_play_sound(const u8 ch, const u8 *data, const u16 freq, const u8 vol) {
  const sound_t *snd = snd_cache_find(data);
  if (!snd) {
    printf("snd_play_sound(%p): unknown sound\n", data);
    return;
  }
  const u32 chmask = SPU_VOICECH((u32)ch);
  if (snd->size && snd->spuaddr >= 0) {
    const s16 vvol = (s16)vol << 8;
    SPU_VOICE(ch)->vol_left = vvol;
    SPU_VOICE(ch)->vol_right = vvol;
    SPU_VOICE(ch)->sample_startaddr = ((u32)snd->spuaddr >> 3);
    snd_key_mask |= chmask;
    spu_key_on(chmask);
    SPU_VOICE(ch)->sample_rate = freq2pitch(freq);
  }
}

void snd_stop_sound(const u8 ch) {
  const u32 chmask = SPU_VOICECH((u32)ch);
  snd_key_mask &= chmask;
  spu_key_off(chmask);
}

void snd_stop_all(void) {
  spu_key_off(0xFFFFFF); // kill all voices
}

void snd_set_sound_vol(const u8 ch, const u8 vol) {
  const s16 vvol = (s16)vol << 8;
  SPU_VOICE(ch)->vol_left = vvol;
  SPU_VOICE(ch)->vol_right = vvol;
}

void snd_update(void) {

}

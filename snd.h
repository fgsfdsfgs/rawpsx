#pragma once

#include "types.h"

#define MAX_SOUNDS 160 // ~110 sounds in game + MOD instruments

enum sound_type {
  SND_TYPE_RAW_PCM,
  SND_TYPE_PCM_WITH_HEADER,
  SND_TYPE_VAG,
};

typedef struct sound sound_t;

void snd_init(void);
void snd_play_sound(const u8 ch, const u8 *data, const u16 freq, const u8 vol);
void snd_stop_sound(const u8 ch);
void snd_stop_all(void);
void snd_set_sound_vol(const u8 ch, const u8 vol);
void snd_update(void);

void snd_clear_cache(void);
sound_t *snd_cache_sound(const u8 *data, u16 size, const int  type);

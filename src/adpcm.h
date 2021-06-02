#pragma once

#include "types.h"

int adpcm_pack_mono_s8(u8 *out, int out_size, const s8 *pcm, int pcm_size, int loopstart, int loopend);

#include <stdio.h>

#include "types.h"
#include "adpcm.h"
#include "util.h"

/* taken from psxsdk, 16-bit support thrown out */

#define PCM_CHUNK_SIZE   28 // num pcm samples for one adpcm block
#define PCM_BUFFER_SIZE  (PCM_CHUNK_SIZE * 128) // size of conversion buffer

#define ADPCM_BLOCK_SIZE 16 // size of one ADPCM block in bytes
#define FLAG_LOOP_END    (1 << 0)
#define FLAG_LOOP_REPEAT (1 << 1)
#define FLAG_LOOP_START  (1 << 2)

/* ADPCM block structure:
 * u8 shift_filter;
 * u8 flags;
 * u8 data[14];
 * Flag bits (`flags` field):
 * bit 0: loop end    - jump to address in voice->sample_repeataddr
 * bit 1: loop repeat - reset ADSR stuff
 * bit 2: loop start  - save current address to voice->sample_repeataddr
 */

static const int factors[5][2] = { 
  {   0,   0 },
  {  60,   0 },
  { 115, -52 },
  {  98, -55 },
  { 122, -60 }
};

static s16 pcm_buffer[PCM_BUFFER_SIZE];

// TODO: get rid of these globals
// they used to be as statics in their corresponding functions,
// but they were never getting reset
static s32 find_s1 = 0;
static s32 find_s2 = 0;
static s32 pack_s1 = 0;
static s32 pack_s2 = 0;

static inline void adpcm_find_predict(const s16 *pcm, s32 *d_samples, s32 *predict_nr, s32 *shift_factor) {
  register int i, j;
  register s32 s0, s1, s2;
  s32 buffer[PCM_CHUNK_SIZE][5];
  s32 min = 0x7FFFFFFF;
  s32 max[5];
  s32 ds;
  s32 min2;
  s32 shift_mask;

  for (i = 0; i < 5; i++) {
    max[i] = 0.0;
    s1 = find_s1;
    s2 = find_s2;
    for (j = 0; j < PCM_CHUNK_SIZE; j ++) {
      s0 = (s32)pcm[j];
      if (s0 > 32767)
        s0 = 32767;
      if (s0 < - 32768)
        s0 = -32768;
      ds = s0 + s1 * factors[i][0] + s2 * factors[i][1];
      buffer[j][i] = ds;
      if (ds > max[i])
        max[i] = ds;
      s2 = s1;
      s1 = s0;
    }
    if (max[i] <= min) {
      min = max[i];
      *predict_nr = i;
    }
    if (min <= 7) {
      *predict_nr = 0;
      break;
    }
  }

  find_s1 = s1;
  find_s2 = s2;
  
  for ( i = 0; i < PCM_CHUNK_SIZE; i++ )
    d_samples[i] = buffer[i][*predict_nr];

  // if (min > 32767)
  //   min = 32767;

  min2 = ( int ) min;
  shift_mask = 0x4000;
  *shift_factor = 0;

  while (*shift_factor < 12) {
    if (shift_mask & (min2 + (shift_mask >> 3)))
      break;
    (*shift_factor)++;
    shift_mask = shift_mask >> 1;
  }
}

static inline void adpcm_do_pack(const s32 *d_samples, s16 *four_bit, const s32 predict_nr, const s32 shift_factor) {
  register int i;
  register s32 s0, di, ds;
  for (i = 0; i < PCM_CHUNK_SIZE; ++i) {
    s0 = d_samples[i] + pack_s1 * factors[predict_nr][0] + pack_s2 * factors[predict_nr][1];
    ds = s0 * (s32)(1 << shift_factor);
    di = ((s32) ds + 0x800) & 0xFFFFF000;
    if (di > 32767)
      di = 32767;
    if (di < -32768)
      di = -32768;
    four_bit[i] = (s32)di;
    di = di >> shift_factor;
    pack_s2 = pack_s1;
    pack_s1 = (s32)di - s0;
  }
}

int adpcm_pack_mono_s8(u8 *out, const int out_size, const s8 *pcm, int pcm_size, const int loop) {
  register int i, j, k;
  register s16 *inptr;
  register u8 *outptr;
  register u8 d;
  s32 d_samples[PCM_CHUNK_SIZE];
  s16 four_bit[PCM_CHUNK_SIZE];
  s32 predict_nr;
  s32 shift_factor;
  int flags = loop ? FLAG_LOOP_START : 0;

  // reset globals
  pack_s1 = pack_s2 = 0;
  find_s1 = find_s2 = 0;

  outptr = out;
  while (pcm_size > 0) {
    // refill buffer
    int size = (pcm_size >= PCM_BUFFER_SIZE) ? PCM_BUFFER_SIZE : pcm_size;
    for (i = 0; i < size; ++i) {
      pcm_buffer[i] = *pcm++;
      pcm_buffer[i] <<= 8;
    }
    // round up to chunk size
    i = size / PCM_CHUNK_SIZE;
    j = size % PCM_CHUNK_SIZE;
    if (j) {
      // fill the rest of the last chunk with silence
      for (; j < PCM_CHUNK_SIZE; ++j)
        pcm_buffer[i * PCM_CHUNK_SIZE + j] = 0;
      ++i;
    }
    // check if it's going to fit
    if (outptr + i * ADPCM_BLOCK_SIZE > out + out_size)
      goto _err_too_big;
    // write out the blocks
    for (j = 0; j < i; ++j) {
      inptr = pcm_buffer + j * PCM_CHUNK_SIZE;
      adpcm_find_predict(inptr, d_samples, &predict_nr, &shift_factor);
      adpcm_do_pack(d_samples, four_bit, predict_nr, shift_factor);
      d = (predict_nr << 4) | shift_factor;
      *outptr++ = d;
      *outptr++ = flags;
      for (k = 0; k < PCM_CHUNK_SIZE; k += 2) {
        d = ((four_bit[k + 1] >> 8) & 0xF0) | ((four_bit[k] >> 12) & 0xF);
        *outptr++ = d;
      }
      pcm_size -= PCM_CHUNK_SIZE;
      if (loop) flags = 0; // reset flags to 0 after setting loop start
    }
  }

  // check if we've run out of space for our extra empty block
  if (outptr + ADPCM_BLOCK_SIZE > out + out_size)
    goto _err_too_big;

  flags = FLAG_LOOP_END | FLAG_LOOP_REPEAT;
  if (!loop) flags |= FLAG_LOOP_START; // loop forever in this chunk
  *outptr++ = (predict_nr << 4) | shift_factor;
  *outptr++ = flags;
  for (i = 0; i < PCM_CHUNK_SIZE / 2; ++i)
    *outptr++ = 0;

  return outptr - out;

_err_too_big:
  printf("adpcm_pack(out_size=%d pcm_size=%d): adpcm data too large for out\n", out_size, pcm_size);
  return -1;
}

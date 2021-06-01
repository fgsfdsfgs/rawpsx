#include <stdio.h>
#include "types.h"
#include "util.h"
#include "unpack.h"

typedef struct {
  int size;
  u32 crc;
  u32 bits;
  u8 *dst;
  const u8 *src;
} unpack_ctx_t;

static int next_bit(unpack_ctx_t *uc) {
  int carry = (uc->bits & 1) != 0;
  uc->bits >>= 1;
  if (uc->bits == 0) { // getnextlwd
    uc->bits = read32be(uc->src); uc->src -= 4;
    uc->crc ^= uc->bits;
    carry = (uc->bits & 1) != 0;
    uc->bits = (1 << 31) | (uc->bits >> 1);
  }
  return carry;
}

static int get_bits(unpack_ctx_t *uc, int count) { // rdd1bits
  int bits = 0;
  for (int i = 0; i < count; ++i) {
    bits <<= 1;
    if (next_bit(uc)) {
      bits |= 1;
    }
  }
  return bits;
}

static void copy_literal(unpack_ctx_t *uc, int num_bits, int len) { // getd3chr
  int count = get_bits(uc, num_bits) + len + 1;
  uc->size -= count;
  if (uc->size < 0) {
    count += uc->size;
    uc->size = 0;
  }
  for (int i = 0; i < count; ++i) {
    *(uc->dst - i) = (u8)get_bits(uc, 8);
  }
  uc->dst -= count;
}

static void copy_reference(unpack_ctx_t *uc, int num_bits, int count) { // copyd3bytes
  uc->size -= count;
  if (uc->size < 0) {
    count += uc->size;
    uc->size = 0;
  }
  const int offset = get_bits(uc, num_bits);
  for (int i = 0; i < count; ++i) {
    *(uc->dst - i) = *(uc->dst - i + offset);
  }
  uc->dst -= count;
}

int bytekiller_unpack(u8 *dst, int dstsize, const u8 *src, int srcsize) {
  unpack_ctx_t uc;
  uc.src = src + srcsize - 4;
  uc.size = read32be(uc.src); uc.src -= 4;
  if (uc.size > dstsize) {
    printf("unpack(%p, %d, %p, %d): invalid unpack size %d, buffer size %d",
      dst, dstsize, src, srcsize, uc.size, dstsize);
    return 0;
  }
  uc.dst = dst + uc.size - 1;
  uc.crc = read32be(uc.src); uc.src -= 4;
  uc.bits = read32be(uc.src); uc.src -= 4;
  uc.crc ^= uc.bits;
  do {
    if (!next_bit(&uc)) {
      if (!next_bit(&uc)) {
        copy_literal(&uc, 3, 0);
      } else {
        copy_reference(&uc, 8, 2);
      }
    } else {
      const int code = get_bits(&uc, 2);
      switch (code) {
      case 3:
        copy_literal(&uc, 8, 8);
        break;
      case 2:
        copy_reference(&uc, 12, get_bits(&uc, 8) + 1);
        break;
      case 1:
        copy_reference(&uc, 10, 4);
        break;
      case 0:
        copy_reference(&uc, 9, 3);
        break;
      }
    }
  } while (uc.size > 0);
  return uc.crc == 0;
}


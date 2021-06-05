#include <string.h>

#include "types.h"
#include "res.h"
#include "cd.h"
#include "gfx.h"
#include "util.h"
#include "unpack.h"
#include "tables.h"
#include "snd.h"
#include "game.h"

u8 *res_seg_code;
u8 *res_seg_video[2];
u8 *res_seg_video_pal;
int res_vidseg_idx;
u16 res_next_part;
u16 res_cur_part;
int res_have_password;
const string_t *res_str_tab;

static mementry_t res_memlist[NUM_MEMLIST_ENTRIES + 1];
static u16 res_memlist_num;

static u8 res_mem[MEMBLOCK_SIZE];

static u8 *res_script_ptr;
static u8 *res_script_membase;
static u8 *res_vid_ptr;
static u8 *res_vid_membase;

typedef struct {
  u8 me_pal;
  u8 me_code;
  u8 me_vid1;
  u8 me_vid2;
} mempart_t;

static const mempart_t res_memlist_parts[] = {
  { 0x14, 0x15, 0x16, 0x00 }, // 16000 - protection screens
  { 0x17, 0x18, 0x19, 0x00 }, // 16001 - introduction
  { 0x1A, 0x1B, 0x1C, 0x11 }, // 16002 - water
  { 0x1D, 0x1E, 0x1F, 0x11 }, // 16003 - jail
  { 0x20, 0x21, 0x22, 0x11 }, // 16004 - 'cite'
  { 0x23, 0x24, 0x25, 0x00 }, // 16005 - 'arene'
  { 0x26, 0x27, 0x28, 0x11 }, // 16006 - 'luxe'
  { 0x29, 0x2A, 0x2B, 0x11 }, // 16007 - 'final'
  { 0x7D, 0x7E, 0x7F, 0x00 }, // 16008 - password screen
  { 0x7D, 0x7E, 0x7F, 0x00 }  // 16009 - password screen
};

void res_init(void) {
  cd_init();

  // read memlist
  cd_file_t *f = cd_fopen(MEMLIST_FILENAME, 0);
  if (!f) panic("res_init(): could not open data files");

  res_memlist_num = 0;
  mementry_t *me = res_memlist;
  while (!cd_feof(f)) {
    ASSERT(res_memlist_num < NUM_MEMLIST_ENTRIES + 1);
    cd_freadordie(me, sizeof(*me), 1, f);
    me->bufptr = NULL;
    me->bank_pos = bswap32(me->bank_pos);
    me->packed_size = bswap32(me->packed_size);
    me->unpacked_size = bswap32(me->unpacked_size);
    // terminating entry
    if (me->status == 0xFF) break;
    ++me;
    ++res_memlist_num;
  }

  cd_fclose(f);

  printf("res_init(): memlist_num=%d\n", (int)res_memlist_num);

  // check if there's a password screen
  const int pwnum = res_memlist_parts[PART_PASSWORD - PART_BASE].me_code;
  char bank[16];
  ASSERT(pwnum < res_memlist_num);
  snprintf(bank, sizeof(bank), BANK_FILENAME, res_memlist[pwnum].bank);
  res_have_password = cd_fexists(bank);

  // set up memory work areas
  res_script_membase = res_script_ptr = res_mem;
  res_vid_membase = res_vid_ptr = res_mem + MEMBLOCK_SIZE - 0x800 * 16;

  // assume english
  res_str_tab = str_tab_en;
}

void res_invalidate_res(void) {
  for (u16 i = 0; i < res_memlist_num; ++i) {
    mementry_t *me = res_memlist + i;
    if (me->type <= RT_BITMAP || me->type > RT_BANK)
      me->status = RS_NULL;
  }
  res_script_ptr = res_script_membase;
  gfx_invalidate_palette();
  snd_clear_cache();
}

void res_invalidate_all(void) {
  for (u16 i = 0; i < res_memlist_num; ++i)
    res_memlist[i].status = RS_NULL;
  res_script_ptr = res_mem;
  gfx_invalidate_palette();
  snd_clear_cache();
}

static int res_read_bank(const mementry_t *me, u8 *out) {
  int ret = 0;
  char fname[16];
  u32 count = 0;
  snprintf(fname, sizeof(fname), BANK_FILENAME, (int)me->bank);
  cd_file_t *f = cd_fopen(fname, 1); // allow reopening same handle because we fseek immediately afterwards
  if (f) {
    cd_fseek(f, me->bank_pos, SEEK_SET);
    count = cd_fread(out, me->packed_size, 1, f);
    cd_fclose(f);
    ret = (count == me->packed_size);
    if (ret && (me->packed_size != me->unpacked_size)) {
      printf("res_read_bank(%d, %p): unpacking %d to %d (%p)\n", me - res_memlist, out, me->packed_size, me->unpacked_size, out);
      ret = bytekiller_unpack(out, me->unpacked_size, out, me->packed_size);
    }
  }
  printf("res_read_bank(%d, %p): bank %d ofs %d count %d packed %d unpacked %d\n", me - res_memlist, out, me->bank, me->bank_pos, count, me->packed_size, me->unpacked_size);
  return ret;
}

static void res_do_load(void) {
  while (1) {
    // find pending entry with max rank
    mementry_t *me = NULL;
    u8 max_rank = 0;
    for (u16 i = 0; i < res_memlist_num; ++i) {
      mementry_t *it = res_memlist + i;
      if (it->status == RS_TOLOAD && it->rank >= max_rank) {
        me = it;
        max_rank = it->rank;
      }
    }
    if (!me) break;

    const int resnum = me - res_memlist;
    u8 *memptr = NULL;
    if (me->type == RT_BITMAP) {
      memptr = res_vid_ptr;
    } else {
      memptr = res_script_ptr;
      // video data seg is after the script data seg, check if they'll intersect
      if (me->unpacked_size > (u32)(res_vid_membase - res_script_ptr)) {
        printf("res_do_load(): not enough memory to load resource %d\n", resnum);
        me->status = RS_NULL;
        continue;
      }
    }

    if (me->bank == 0) {
      printf("res_do_load(): res %d has NULL banknum\n", resnum);
      me->status = RS_NULL;
    } else {
      if (res_read_bank(me, memptr)) {
        printf("res_do_load(): read res %d (type %d) from bank %d\n", me - res_memlist, me->type, me->bank);
        if (me->type == RT_BITMAP) {
          gfx_blit_bitmap(res_vid_ptr, me->unpacked_size);
          me->status = RS_NULL;
        } else {
          me->bufptr = memptr;
          me->status = RS_LOADED;
          res_script_ptr += me->unpacked_size;
          if (me->type == RT_SOUND) {
            printf("res_do_load(): precaching sound %d size %d\n", resnum, me->unpacked_size);
            snd_cache_sound(me->bufptr, me->unpacked_size, SND_TYPE_PCM_WITH_HEADER);
          }
        }
      } else if (me->bank == 12 && me->type == RT_BANK) {
        // DOS demo does not have this resource, ignore it
        me->status = RS_NULL;
        continue;
      } else {
        panic("res_do_load(): could not load resource %d from bank %d", resnum, (int)me->bank);
      }
    }
  }
}

void res_setup_part(const u16 part_id) {
  if (part_id != res_cur_part) {
    if (part_id < PART_BASE || part_id > PART_LAST)
      panic("res_setup_part(%05d): invalid part", (int)part_id);

    const mempart_t part = res_memlist_parts[part_id - PART_BASE];
    res_invalidate_all();

    res_memlist[part.me_pal ].status = RS_TOLOAD;
    res_memlist[part.me_code].status = RS_TOLOAD;
    res_memlist[part.me_vid1].status = RS_TOLOAD;
    if (part.me_vid2 != 0)
      res_memlist[part.me_vid2].status = RS_TOLOAD;
    res_do_load();

    res_seg_video_pal = res_memlist[part.me_pal].bufptr;
    res_seg_code = res_memlist[part.me_code].bufptr;
    res_seg_video[0] = res_memlist[part.me_vid1].bufptr;
    if (part.me_vid2 != 0)
      res_seg_video[1] = res_memlist[part.me_vid2].bufptr;
  
    res_cur_part = part_id;
  }

  res_script_membase = res_script_ptr;
}

void res_load(const u16 res_id) {
  if (res_id > PART_BASE) {
    res_next_part = res_id;
    return;
  }
  mementry_t *me = res_memlist + res_id;
  if (me->status == RS_NULL) {
    me->status = RS_TOLOAD;
    res_do_load();
  }
}

const mementry_t *res_get_entry(const u16 res_id) {
  if (res_id >= PART_BASE)
    return NULL;
  return res_memlist + res_id;
}

const char *res_get_string(const string_t *strtab, const u16 str_id) {
  if (strtab == NULL) strtab = res_str_tab;
  if (strtab == NULL) return NULL;
  for (u16 i = 0; i < 0x100 && strtab[i].id != 0xFFFF; ++i) {
    if (strtab[i].id == str_id)
      return strtab[i].str;
  }
  return NULL;
}

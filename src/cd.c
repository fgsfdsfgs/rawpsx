#include <stdio.h>
#include <string.h>
#include <psxetc.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <psxcd.h>

#include "types.h"
#include "cd.h"
#include "util.h"

// TEMPORARY CD FILE READING API WITH BUFFERS AND SHIT
// copied straight from d2d-psx and converted to only use one static handle

#define SECSIZE 2048
#define BUFSECS 4
#define BUFSIZE (BUFSECS * SECSIZE)
#define MAX_FHANDLES 1

static const u32 cdmode = CdlModeSpeed;

struct cd_file_s {
  char fname[64];
  CdlFILE cdf;
  s32 secstart, secend, seccur;
  s32 fp, bufp;
  s32 bufleft;
  unsigned char buf[BUFSIZE];
};

// lmao 1handle
static cd_file_t fhandle;
static s32 num_fhandles = 0;

void cd_init(void) {
  CdInit();
  // look alive
  CdControl(CdlNop, 0, 0);
  CdStatus();
  // set hispeed mode
  CdControlB(CdlSetmode, (u8 *)&cdmode, 0);
  VSync(3); // have to do this to not explode the drive apparently
}

cd_file_t *cd_fopen(const char *fname, const int reopen) {
  // check if the same file was just open and return it if allowed
  if (reopen && !strncmp(fhandle.fname, fname, sizeof(fhandle.fname))) {
    num_fhandles++;
    return &fhandle;
  }

  if (num_fhandles >= MAX_FHANDLES) {
    printf("cd_fopen(%s): too many file handles\n", fname);
    return NULL;
  }

  cd_file_t *f = &fhandle;
  memset(f, 0, sizeof(*f));

  if (CdSearchFile(&f->cdf, fname) == NULL) {
    printf("cd_fopen(%s): file not found\n", fname);
    return NULL;
  }

  // read first sector of the file
  CdControl(CdlSetloc, (u8 *)&f->cdf.pos, 0);
  CdRead(BUFSECS, (u32 *)f->buf, CdlModeSpeed);
  CdReadSync(0, NULL);

  // set fp and shit
  f->secstart = CdPosToInt(&f->cdf.pos);
  f->seccur = f->secstart;
  f->secend = f->secstart + (f->cdf.size + SECSIZE-1) / SECSIZE;
  f->fp = 0;
  f->bufp = 0;
  f->bufleft = (f->cdf.size >= BUFSIZE) ? BUFSIZE : f->cdf.size;
  strncpy(fhandle.fname, fname, sizeof(fhandle.fname) - 1);

  num_fhandles++;
  printf("cd_fopen(%s): size %u bufleft %d secs %d %d\n", fname, f->cdf.size, f->bufleft, f->secstart, f->secend);

  return f;
}

int cd_fexists(const char *fname) {
  CdlFILE cdf;
  if (CdSearchFile(&cdf, (char *)fname) == NULL) {
    printf("cd_fexists(%s): file not found\n", fname);
    return 0;
  }
  return 1;
}

void cd_fclose(cd_file_t *f) {
  if (!f) return;
  num_fhandles--;
}

s32 cd_fread(void *ptr, s32 size, s32 num, cd_file_t *f) {
  s32 rx, rdbuf;
  s32 fleft;
  CdlLOC pos;

  if (!f || !ptr) return -1;
  if (!size) return 0;

  size *= num;
  rx = 0;

  while (size) {
    // first empty the buffer
    rdbuf = (size > f->bufleft) ? f->bufleft : size;
    memcpy(ptr, f->buf + f->bufp, rdbuf);
    rx += rdbuf;
    ptr += rdbuf;
    f->fp += rdbuf;
    f->bufp += rdbuf;
    f->bufleft -= rdbuf;
    size -= rdbuf;

    // if we went over, load next sector
    if (f->bufleft == 0) {
      f->seccur += BUFSECS;
      // check if we have reached the end
      if (f->seccur >= f->secend)
        return rx;
      // looks like you need to seek every time when you use CdRead
      CdIntToPos(f->seccur, &pos);
      CdControl(CdlSetloc, (u8 *)&pos, 0);
      CdRead(BUFSECS, (u32 *)f->buf, CdlModeSpeed);
      CdReadSync(0, 0);
      fleft = f->cdf.size - f->fp;
      f->bufleft = (fleft >= BUFSIZE) ? BUFSIZE: fleft;
      f->bufp = 0;
    }
  }

  return rx;
}

void cd_freadordie(void *ptr, s32 size, s32 num, cd_file_t *f) {
  if (cd_fread(ptr, size, num, f) < 0)
    panic("cd_freadordie(%.16s, %d, %d): fucking died", f->cdf.name, size, num);
}

s32 cd_fseek(cd_file_t *f, s32 ofs, s32 whence) {
  s32 fsec, bofs;
  CdlLOC pos;

  if (!f) return -1;

  if (whence == SEEK_CUR)
    ofs = f->fp + ofs;

  if (f->fp == ofs) return 0;

  fsec = f->secstart + (ofs / BUFSIZE) * BUFSECS;
  bofs = ofs % BUFSIZE;

  // fuck SEEK_END, it's only used to get file length here

  if (fsec != f->seccur) {
    // sector changed; seek to new one and buffer it
    CdIntToPos(fsec, &pos);
    CdControl(CdlSetloc, (u8 *)&pos, 0);
    CdRead(BUFSECS, (u32 *)f->buf, CdlModeSpeed);
    CdReadSync(0, 0);
    f->seccur = fsec;
    f->bufp = -1; // hack: see below
  }

  if (bofs != f->bufp) {
    // buffer offset changed (or new sector loaded); reset pointers
    f->bufp = bofs;
    f->bufleft = BUFSIZE - bofs;
    if (f->bufleft < 0) f->bufleft = 0;
  }

  f->fp = ofs;

  return 0;
}

s32 cd_ftell(cd_file_t *f) {
  if (!f) return -1;
  return f->fp;
}

s32 cd_fsize(cd_file_t *f) {
  if (!f) return -1;
  return f->cdf.size;
}

int cd_feof(cd_file_t *f) {
  if (!f) return -1;
  return (f->seccur >= f->secend);
}

u8 cd_fread_u8(cd_file_t *f) {
  u8 res = 0;
  cd_freadordie(&res, 1, 1, f);
  return res;
}

u16 cd_fread_u16be(cd_file_t *f) {
  u16 res = 0;
  cd_freadordie(&res, 2, 1, f);
  return bswap16(res);
}

u32 cd_fread_u32be(cd_file_t *f) {
  u32 res = 0;
  cd_freadordie(&res, 4, 1, f);
  return bswap32(res);
}

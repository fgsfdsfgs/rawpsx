#pragma once

#include <stdio.h>
#include "types.h"

typedef struct cd_file_s cd_file_t;

void cd_init(void);
cd_file_t *cd_fopen(const char *fname, const int reopen);
int cd_fexists(const char *fname);
void cd_fclose(cd_file_t *f);
s32 cd_fread(void *ptr, s32 size, s32 num, cd_file_t *f);
void cd_freadordie(void *ptr, s32 size, s32 num, cd_file_t *f);
s32 cd_fseek(cd_file_t *f, s32 ofs, int whence);
s32 cd_ftell(cd_file_t *f);
s32 cd_fsize(cd_file_t *f);
int cd_feof(cd_file_t *f);

u8 cd_fread_u8(cd_file_t *f);
u16 cd_fread_u16be(cd_file_t *f);
u32 cd_fread_u32be(cd_file_t *f);

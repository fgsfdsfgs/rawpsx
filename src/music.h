#pragma once

#include "types.h"

void mus_init(void);
void mus_load(const u16 resid, const u16 delay, const u8 pos);
void mus_start(void);
void mus_set_delay(const u16 delay);
void mus_stop(void);
void mus_update(void);

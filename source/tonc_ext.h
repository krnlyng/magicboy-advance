// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024 Franz-Josef Haider

#ifndef TONC_EXT__
#define TONC_EXT__

/* These are my custom extensions to libtonc for printing characters upside down */

#include <tonc.h>
#include <tonc_video.h>

void bmp16_drawg_b1cts_ud(uint gid);
int	tte_write_ud(const char *text);
void memcpy16_rev(u16 *target, u16 *source, int length);
void memset16_rev(u16 *target, u16 value, int length);
void sbmp16_blit_ud(const TSurface *dst, int dstX, int dstY,
    uint width, uint height, const TSurface *src, int srcX, int srcY);
void sbmp16_rect_ud(const TSurface *dst,
    int left, int top, int right, int bottom, u32 clr);

#endif


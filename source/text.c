// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024 Franz-Josef Haider

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <tonc.h>
#include <tonc_video.h>

#include "text.h"
#include "tonc_ext.h"

#define MAX_TEXT_LEN 256

#define TOTAL_SYMBOLS_IN_SURFACE 12
#define MINUS_POSITION 10
#define DOT_POSITION 11

extern u16 VCR_OSD_MONO_NUMBERS_HUGE[];
extern u32 VCR_OSD_MONO_NUMBERS_HUGE_SIZE;
extern u16 VCR_OSD_MONO_NUMBERS_LARGE[];
extern u32 VCR_OSD_MONO_NUMBERS_LARGE_SIZE;

static TSurface surface_huge_numbers;
static TSurface surface_large_numbers;
static TSurface surface_large_numbers_red;
static TSurface surface_large_numbers_blue;
static TSurface surface_large_numbers_orange;
static TSurface surface_large_numbers_magenta;
static TSurface surface_large_numbers_green;

#define VCR_OSD_MONO_NUMBERS_HUGE_WIDTH 60
#define VCR_OSD_MONO_NUMBERS_HUGE_HEIGHT 104

#define VCR_OSD_MONO_NUMBERS_LARGE_WIDTH 30
#define VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT 52

u16 *VCR_OSD_MONO_NUMBERS_LARGE_RED = 0;
u16 *VCR_OSD_MONO_NUMBERS_LARGE_BLUE = 0;
u16 *VCR_OSD_MONO_NUMBERS_LARGE_ORANGE = 0;
u16 *VCR_OSD_MONO_NUMBERS_LARGE_MAGENTA = 0;
u16 *VCR_OSD_MONO_NUMBERS_LARGE_GREEN = 0;

void initializeText()
{
    tte_init_bmp(3, &sys8Font, NULL);
    REG_DISPCNT = DCNT_MODE3 | DCNT_BG2;
}

int convertColor(int col)
{
    int color = CLR_WHITE;
    switch (col) {
        case COLOR_GREEN:
            color = CLR_GREEN;
            break;
        case COLOR_RED:
            color = CLR_RED;
            break;
        case COLOR_BLUE:
            color = CLR_BLUE;
            break;
        case COLOR_YELLOW:
            color = CLR_YELLOW;
            break;
        case COLOR_MAGENTA:
            color = CLR_MAG;
            break;
        case COLOR_CYAN:
            color = CLR_CYAN;
            break;
        case COLOR_ORANGE:
            color = CLR_ORANGE;
            break;
        case COLOR_PURPLE:
            color = CLR_PURPLE;
            break;
        case COLOR_FUCHSIA:
            color = CLR_FUCHSIA;
            break;
        case COLOR_LIME:
            color = CLR_LIME;
            break;
        case COLOR_CREAM:
            color = CLR_CREAM;
            break;
        case COLOR_GRAY:
            color = CLR_GRAY;
            break;
        default:
            color = CLR_WHITE;
    };

    return color;
}

void printTextColor(int row, int column, int fillcolumn, int col, int ud, char *fmt, ...)
{
    va_list args;
    char buf[MAX_TEXT_LEN];

    va_start(args, fmt);
    vsnprintf(buf, MAX_TEXT_LEN, fmt, args);
    va_end(args);

    tte_set_color(TTE_INK, convertColor(col));
    printText(row, column, fillcolumn, ud, buf);
}

int getGlyphWidth(void)
{
    return tte_get_glyph_width(0);
}

int getGlyphHeight(void)
{
    return tte_get_glyph_height(0);
}

int getLargeGlyphWidth(void)
{
    return VCR_OSD_MONO_NUMBERS_LARGE_WIDTH;
}

int getLargeGlyphHeight(void)
{
    return VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT;
}

int getScreenWidth(void)
{
    return M3_WIDTH;
}

int getScreenHeight(void)
{
    return M3_HEIGHT;
}

void printText(int row, int column, int fillcolumn, int ud, char *fmt, ...)
{
    va_list args;
    char buf[MAX_TEXT_LEN];
    int h, gh;

    va_start(args, fmt);
    vsnprintf(buf, MAX_TEXT_LEN, fmt, args);
    va_end(args);

    gh = tte_get_glyph_height(0);
    h = row * gh;
    if (ud) {
        tte_set_pos(column, h + gh);
        TTC *tc = tte_get_context();
        sbmp16_rect(&tc->dst, column, h, column + fillcolumn, h + gh, tc->cattr[TTE_PAPER]);

        int textlen = 0;
        for (int i = 0; i < strlen(buf); i++) {
            if ((buf[i] == '#') && (i + 1 < strlen(buf)) && (buf[i+1] == '{')) {
                i += 2;

                while (i < strlen(buf)) {
                    if (buf[i] == '}') break;
                    i++;
                }
            } else {
                textlen++;
            }
        }
        int s = min((fillcolumn) / getGlyphWidth(), getScreenWidth() / getGlyphWidth());

        char tmpbuf[MAX_TEXT_LEN];
        strcpy(tmpbuf, buf);
        while (s > textlen + 1) {
            snprintf(buf, MAX_TEXT_LEN, " %s", tmpbuf);
            strcpy(tmpbuf, buf);
            textlen++;
        }
        tte_write_ud(tmpbuf);
    } else {
        tte_set_pos(column, h);
        TTC *tc = tte_get_context();
        sbmp16_rect(&tc->dst, column, h, column + fillcolumn, h + gh, tc->cattr[TTE_PAPER]);
        tte_write(buf);
    }
}

void initializeHugeNumbers()
{
    srf_init(&surface_huge_numbers,
        SRF_BMP16,
        VCR_OSD_MONO_NUMBERS_HUGE,
        VCR_OSD_MONO_NUMBERS_HUGE_WIDTH * TOTAL_SYMBOLS_IN_SURFACE, // including -.
        VCR_OSD_MONO_NUMBERS_HUGE_HEIGHT,
        16,
        pal_bg_mem);
}

void initializeLargeNumbers()
{
    srf_init(&surface_large_numbers,
        SRF_BMP16,
        VCR_OSD_MONO_NUMBERS_LARGE,
        VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * TOTAL_SYMBOLS_IN_SURFACE, // including -.
        VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT,
        16,
        pal_bg_mem);

    VCR_OSD_MONO_NUMBERS_LARGE_RED = malloc(VCR_OSD_MONO_NUMBERS_LARGE_SIZE);
    for (int i = 0; i < VCR_OSD_MONO_NUMBERS_LARGE_SIZE / sizeof(u16); i++) {
        VCR_OSD_MONO_NUMBERS_LARGE_RED[i] = (VCR_OSD_MONO_NUMBERS_LARGE[i] == 0) ? 0 : 0x1f;
    }

    VCR_OSD_MONO_NUMBERS_LARGE_BLUE = malloc(VCR_OSD_MONO_NUMBERS_LARGE_SIZE);
    for (int i = 0; i < VCR_OSD_MONO_NUMBERS_LARGE_SIZE / sizeof(u16); i++) {
        VCR_OSD_MONO_NUMBERS_LARGE_BLUE[i] = (VCR_OSD_MONO_NUMBERS_LARGE[i] == 0) ? 0 : CLR_BLUE;
    }

    VCR_OSD_MONO_NUMBERS_LARGE_ORANGE = malloc(VCR_OSD_MONO_NUMBERS_LARGE_SIZE);
    for (int i = 0; i < VCR_OSD_MONO_NUMBERS_LARGE_SIZE / sizeof(u16); i++) {
        VCR_OSD_MONO_NUMBERS_LARGE_ORANGE[i] = (VCR_OSD_MONO_NUMBERS_LARGE[i] == 0) ? 0 : CLR_ORANGE;
    }

    VCR_OSD_MONO_NUMBERS_LARGE_MAGENTA = malloc(VCR_OSD_MONO_NUMBERS_LARGE_SIZE);
    for (int i = 0; i < VCR_OSD_MONO_NUMBERS_LARGE_SIZE / sizeof(u16); i++) {
        VCR_OSD_MONO_NUMBERS_LARGE_MAGENTA[i] = (VCR_OSD_MONO_NUMBERS_LARGE[i] == 0) ? 0 : CLR_MAG;
    }

    VCR_OSD_MONO_NUMBERS_LARGE_GREEN = malloc(VCR_OSD_MONO_NUMBERS_LARGE_SIZE);
    for (int i = 0; i < VCR_OSD_MONO_NUMBERS_LARGE_SIZE / sizeof(u16); i++) {
        VCR_OSD_MONO_NUMBERS_LARGE_GREEN[i] = (VCR_OSD_MONO_NUMBERS_LARGE[i] == 0) ? 0 : CLR_GREEN;
    }

    srf_init(&surface_large_numbers_red,
        SRF_BMP16,
        VCR_OSD_MONO_NUMBERS_LARGE_RED,
        VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * TOTAL_SYMBOLS_IN_SURFACE, // including -.
        VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT,
        16,
        pal_bg_mem);

    srf_init(&surface_large_numbers_blue,
        SRF_BMP16,
        VCR_OSD_MONO_NUMBERS_LARGE_BLUE,
        VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * TOTAL_SYMBOLS_IN_SURFACE, // including -.
        VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT,
        16,
        pal_bg_mem);

    srf_init(&surface_large_numbers_orange,
        SRF_BMP16,
        VCR_OSD_MONO_NUMBERS_LARGE_ORANGE,
        VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * TOTAL_SYMBOLS_IN_SURFACE, // including -.
        VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT,
        16,
        pal_bg_mem);

    srf_init(&surface_large_numbers_magenta,
        SRF_BMP16,
        VCR_OSD_MONO_NUMBERS_LARGE_MAGENTA,
        VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * TOTAL_SYMBOLS_IN_SURFACE, // including -.
        VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT,
        16,
        pal_bg_mem);

    srf_init(&surface_large_numbers_green,
        SRF_BMP16,
        VCR_OSD_MONO_NUMBERS_LARGE_GREEN,
        VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * TOTAL_SYMBOLS_IN_SURFACE, // including -.
        VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT,
        16,
        pal_bg_mem);

    TSurface *dst = tte_get_surface();
    srf_pal_copy(dst, &surface_huge_numbers, 16);
}

void printHugeNumber(int number)
{
    int offset_x = 0;
    int offset_y = (SCREEN_HEIGHT - VCR_OSD_MONO_NUMBERS_HUGE_HEIGHT) / 2;
    int negative = 0;

    if (number < 0) {
        TSurface *dst = tte_get_surface();
        sbmp16_blit(dst, offset_x, offset_y, VCR_OSD_MONO_NUMBERS_HUGE_WIDTH, VCR_OSD_MONO_NUMBERS_HUGE_HEIGHT, &surface_huge_numbers, VCR_OSD_MONO_NUMBERS_HUGE_WIDTH * 10, 0);
        number = -number;
        negative = 1;
    }

    for (int i = 0; i < 3; i++) {
        if (number == 0 && i > 0) {
            if (i == 2 && negative) {
                break;
            }
            TSurface *dst = tte_get_surface();
            sbmp16_rect(dst, VCR_OSD_MONO_NUMBERS_HUGE_WIDTH * (3 - (i + 1)) + offset_x, offset_y, VCR_OSD_MONO_NUMBERS_HUGE_WIDTH * (3 - (i + 1)) + offset_x + VCR_OSD_MONO_NUMBERS_HUGE_WIDTH, offset_y + VCR_OSD_MONO_NUMBERS_HUGE_HEIGHT, CLR_BLACK);
        } else {
            int digit = (number % 10);
            TSurface *dst = tte_get_surface();
            sbmp16_blit(dst, VCR_OSD_MONO_NUMBERS_HUGE_WIDTH * (3 - (i + 1)) + offset_x, offset_y, VCR_OSD_MONO_NUMBERS_HUGE_WIDTH, VCR_OSD_MONO_NUMBERS_HUGE_HEIGHT, &surface_huge_numbers, digit * VCR_OSD_MONO_NUMBERS_HUGE_WIDTH, 0);
            number /= 10;
        }
    }
}

void printLargeNumber(int offset_x, int offset_y, int number, int col, int ud, int withdot)
{
    int negative = 0;

    TSurface *surface = 0;
    switch (col) {
        case COLOR_RED:
            surface = &surface_large_numbers_red;
            break;
        case COLOR_BLUE:
            surface = &surface_large_numbers_blue;
            break;
        case COLOR_ORANGE:
            surface = &surface_large_numbers_orange;
            break;
        case COLOR_GREEN:
            surface = &surface_large_numbers_green;
            break;
        case COLOR_MAGENTA:
            surface = &surface_large_numbers_magenta;
            break;
        default:
            surface = &surface_large_numbers;
    };

    if (number < 0) {
        TSurface *dst = tte_get_surface();
        if (ud) {
            sbmp16_blit_ud(dst, offset_x, offset_y, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT, surface, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * MINUS_POSITION, 0);
        } else {
            sbmp16_blit(dst, offset_x, offset_y, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT, surface, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * MINUS_POSITION, 0);
        }
        number = -number;
        negative = 1;
    }

    for (int i = 0; i < 3; i++) {
        if (number == 0 && i > 0) {
            if (i == 2 && negative) {
                break;
            }
            TSurface *dst = tte_get_surface();
            if (ud) {
                sbmp16_rect_ud(dst, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * (3 - (i + 1)) + offset_x, offset_y, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * (3 - (i + 1)) + offset_x + VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, offset_y + VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT, CLR_BLACK);
            } else {
                sbmp16_rect(dst, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * (3 - (i + 1)) + offset_x, offset_y, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * (3 - (i + 1)) + offset_x + VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, offset_y + VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT, CLR_BLACK);
            }
        } else {
            int digit = (number % 10);
            TSurface *dst = tte_get_surface();
            if (ud) {
                sbmp16_blit_ud(dst, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * (3 - (i + 1)) + offset_x, offset_y, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT, surface, digit * VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, 0);
            } else {
                sbmp16_blit(dst, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * (3 - (i + 1)) + offset_x, offset_y, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT, surface, digit * VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, 0);
            }
            number /= 10;
        }
    }

    if (withdot) {
        TSurface *dst = tte_get_surface();
        if (ud) {
            sbmp16_blit_ud(dst, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * 3 + offset_x, offset_y, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT, surface, DOT_POSITION * VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, 0);
        } else {
            sbmp16_blit(dst, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * 3 + offset_x, offset_y, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT, surface, DOT_POSITION * VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, 0);
        }
    } else {
        TSurface *dst = tte_get_surface();
        if (ud) {
            sbmp16_rect_ud(dst, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * 3 + offset_x, offset_y, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * 3 + offset_x + VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, offset_y + VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT, CLR_BLACK);
        } else {
            sbmp16_rect(dst, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * 3 + offset_x, offset_y, VCR_OSD_MONO_NUMBERS_LARGE_WIDTH * 3 + offset_x + VCR_OSD_MONO_NUMBERS_LARGE_WIDTH, offset_y + VCR_OSD_MONO_NUMBERS_LARGE_HEIGHT, CLR_BLACK);
        }
    }
}

void clearScreen()
{
    tte_write("#{es;P}");
}


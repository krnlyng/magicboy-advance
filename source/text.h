// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024 Franz-Josef Haider

#ifndef TEXT_H__
#define TEXT_H__

#include <stdarg.h>

void initializeText();
void initializeLargeNumbers();
void initializeHugeNumbers();
void printText(int row, int column, int fillcolumn, int ud, char *fmt, ...);
void printTextColor(int row, int column, int fillcolumn, int col, int ud, char *fmt, ...);
void printHugeNumber(int number);
void printLargeNumber(int square, int maxSquares, int number, int col, int ud, int withdot);
void clearScreen();
// convert color from enum to hex value
int convertColor(int col);
int getGlyphWidth(void);
int getGlyphHeight(void);
int getScreenWidth(void);
int getLargeGlyphWidth(void);
int getLargeGlyphHeight(void);
int getScreenWidth(void);
int getScreenHeight(void);

enum COLOR {
    COLOR_WHITE = 0,
    COLOR_GREEN,
    COLOR_RED,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_ORANGE,
    COLOR_PURPLE,
    COLOR_FUCHSIA,
    COLOR_LIME,
    COLOR_GRAY,
    COLOR_CREAM,
};

#endif


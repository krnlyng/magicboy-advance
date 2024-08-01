// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2024 Franz-Josef Haider

#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>

#include <stdio.h>
#include <string.h>

#include "text.h"

#include "AAS.h"
#include "AAS_Data.h"

#define FPS 60

#define TIME_CLEAR_LIFE_CHANGED (FPS * 3)
#define TIME_AUTO_SAVE (FPS * 15)

// let's hope it's never 8 (game will take forever)
#define MAX_PLAYERS 8

#define MAX_COMMANDER_DAMAGE 21
#define MAX_POISON_COUNTERS 10

#define MAX_LIFE_FOR_CUSTOM_PRINT 999
#define MIN_LIFE_FOR_CUSTOM_PRINT -99

#define POISON_COLOR COLOR_LIME
#define ENERGY_COLOR COLOR_CREAM
#define EXPERIENCE_COLOR COLOR_GRAY
#define COMMANDERTAX_COLOR COLOR_WHITE

// These are shared with commander damage and thus need the be > MAX_PLAYERS
#define POISON_COUNTER (MAX_PLAYERS + 1)
#define ENERGY_COUNTER (MAX_PLAYERS + 2)
#define EXPERIENCE_COUNTER (MAX_PLAYERS + 3)
#define COMMANDERTAX_COUNTER (MAX_PLAYERS + 4)
#define FIRST_COUNTER POISON_COUNTER
#define LAST_COUNTER COMMANDERTAX_COUNTER
#define LAST_COUNTER_NO_COMMANDERTAX EXPERIENCE_COUNTER

// Save, save and quit and so on.
enum {
    MENU_ITEM_SAVE = 0,
    MENU_ITEM_SAVE_AND_QUIT,
    MENU_ITEM_RETURN,
    MENU_ITEM_FLIP_TOP_NUMBERS,
    MENU_ITEM_SONG,
    MENU_ITEM_SFX,
    MENU_ITEM_CONTROLS,
    MENU_ITEM_QUIT,
    MENU_ITEMS,
};

#define MAX_BACKGROUND_SONGS 3

enum {
    SETUP_ITEM_QUICK_START_COMMANDER4P = 0,
    SETUP_ITEM_QUICK_START_COMMANDER1P,
    SETUP_ITEM_QUICK_START_1V1,
    SETUP_ITEM_STARTING_LIFE,
    SETUP_ITEM_PLAYERS,
    SETUP_ITEM_OPPONENTS,
    SETUP_ITEM_SONG,
    SETUP_ITEM_SFX,
    SETUP_ITEM_START,
    SETUP_ITEM_LOAD_SAVE,
    SETUP_ITEM_LOAD_AUTOSAVE,
    SETUP_ITEM_CONTROLS,
    SETUP_ITEMS,
};

#define FLASH_ROM ((vu8 *) 0x0e000000)
#define FLASH_ROM_SIZE 0x10000

enum {
    STATE_SETUP = 0,
    STATE_COUNTLIFE = 1,
    STATE_MENU = 2,
    STATE_CONTROLS = 3,
};

struct PlayerState {
    int lifeCounter;
    int commanderDamage[MAX_PLAYERS];
    int poisonCounters;
    int energyCounters;
    int experienceCounters;
    int commanderTaxCounter; // (Commander Tax / 2)
};

#define SAVEABLE_GAME_STATE \
    int maxPlayers; \
    int maxOpponents; \
    int startingLife; \
    int upsideDownNumbers; \
    int selectedBackgroundSong; \
    int sfxEnabled; \
    struct PlayerState playerState[MAX_PLAYERS];

struct SaveableGameState {
    SAVEABLE_GAME_STATE;
};

struct GameState {
    SAVEABLE_GAME_STATE;
    int state;
    int previousState;
    int keysDown;
    int framesSinceUPPressedOrQuarterSecond;
    int framesSinceDOWNPressedOrQuarterSecond;
    int selectedPlayer;
    int selectedMenuItem;
    int selectedSetupItem;
    int selectedCommanderDamageOrCounter;
    int lastCounter;
    int triggerAutoSaveInFrames;
    int printedRegular;
    int lifeChangedCurrent;
    int triggerClearLifeChangedCurrentInFrames;
    int stateToReturnTo;
};

static void initializeStartingLifeAndCounters(struct GameState *state)
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        state->playerState[i].lifeCounter = state->startingLife;
        for (int j = 0; j < MAX_PLAYERS; j++) {
            state->playerState[i].commanderDamage[j] = 0;
        }
        state->playerState[i].poisonCounters = 0;
        state->playerState[i].energyCounters = 0;
        state->playerState[i].experienceCounters = 0;
        state->playerState[i].commanderTaxCounter = 0;
    }

    if (state->maxOpponents == 0) {
        state->lastCounter = LAST_COUNTER_NO_COMMANDERTAX;
    } else {
        state->lastCounter = LAST_COUNTER;
    }
}

static void initializeGameState(struct GameState *state)
{
    state->state = STATE_SETUP;
    state->previousState = -1;
    state->stateToReturnTo = -1;

    state->keysDown = 0;
    state->framesSinceUPPressedOrQuarterSecond = 0;
    state->framesSinceDOWNPressedOrQuarterSecond = 0;
    state->selectedPlayer = 0;
    state->selectedMenuItem = 0;
    state->selectedSetupItem = 0;
    // state->selectedBackgroundSong = 0;
    state->sfxEnabled = 1;
    state->selectedCommanderDamageOrCounter = 0;
    state->lastCounter = LAST_COUNTER;
    state->triggerAutoSaveInFrames = 0;
    state->printedRegular = 0;
    state->lifeChangedCurrent = 0;
    state->triggerClearLifeChangedCurrentInFrames = 0;

    state->upsideDownNumbers = 0;

    state->maxPlayers = 4;
    state->maxOpponents = 1;
    state->startingLife = 40;

    initializeStartingLifeAndCounters(state);
}

static void resetNonPersistentGameStateValues(struct GameState *state)
{
    state->state = STATE_COUNTLIFE;
    state->previousState = -1;
    state->stateToReturnTo = -1;

    state->keysDown = 0;
    state->framesSinceUPPressedOrQuarterSecond = 0;
    state->framesSinceDOWNPressedOrQuarterSecond = 0;
    state->selectedPlayer = 0;
    state->selectedCommanderDamageOrCounter = 0;
    if (state->maxOpponents == 0) {
        state->lastCounter = LAST_COUNTER_NO_COMMANDERTAX;
    } else {
        state->lastCounter = LAST_COUNTER;
    }
    state->selectedMenuItem = 0;
    state->selectedSetupItem = 0;
    // state->selectedBackgroundSong = 0;
    state->triggerAutoSaveInFrames = 0;
    state->printedRegular = 0;
    state->lifeChangedCurrent = 0;
    state->triggerClearLifeChangedCurrentInFrames = 0;
}

static int flashValid(int slot)
{
    int is_valid_savefile = (FLASH_ROM[0] == 'G' && FLASH_ROM[1] == 'B' && FLASH_ROM[2] == 'A' && FLASH_ROM[3] == 'L') ? 1 : 0;
    if (is_valid_savefile) {
        if (slot == 0 && FLASH_ROM[4] == 'X') {
            return 1;
        }
        if (slot == 1 && FLASH_ROM[5 + sizeof(struct SaveableGameState)] == 'X') {
            return 1;
        }
    }

    return 0;
}

static void saveState(struct GameState *state, int slot)
{
    FLASH_ROM[0] = 'G';
    FLASH_ROM[1] = 'B';
    FLASH_ROM[2] = 'A';
    FLASH_ROM[3] = 'L';

    // TODO: crc?

    if (slot == 0) {
        FLASH_ROM[4] = 'X';
    } else {
        FLASH_ROM[5 + sizeof(struct SaveableGameState)] = 'X';
    }

    for (int i = 0; i < sizeof(struct SaveableGameState); i++) {
        FLASH_ROM[5 + i + slot * sizeof(struct SaveableGameState) + slot] = ((char*)state)[i];
    }
}

static int loadState(struct GameState *state, int slot)
{
    if (flashValid(slot)) {
        for (int i = 0; i < sizeof(struct SaveableGameState); i++) {
            ((char*)state)[i] = FLASH_ROM[5 + i + slot * sizeof(struct SaveableGameState) + slot];
        }

        resetNonPersistentGameStateValues(state);
        return 1;
    }

    return 0;
}

static void adjustBackgroundSong(struct GameState *state)
{
    AAS_MOD_Stop(AAS_DATA_MOD_musix_retrospective);
    AAS_MOD_Stop(AAS_DATA_MOD_drozerix___ai_renaissance);

    switch (state->selectedBackgroundSong) {
        case 0:
            AAS_MOD_Play(AAS_DATA_MOD_drozerix___ai_renaissance);
            break;
        case 1:
            AAS_MOD_Play(AAS_DATA_MOD_musix_retrospective);
            break;
        case 2:
            break;
    };
}

static int getPlayerColor(int player)
{
    int color = 0;
    switch (player) {
        /* large start */
        case 0:
            color = COLOR_BLUE;
            break;
        case 1:
            color = COLOR_ORANGE;
            break;
        case 2:
            color = COLOR_MAGENTA;
            break;
        case 3:
            color = COLOR_GREEN;
            break;
        /* large end */
        case 4:
            color = COLOR_PURPLE;
            break;
        case 5:
            color = COLOR_CYAN;
            break;
        case 6:
            color = COLOR_CREAM;
            break;
        case 7:
            color = COLOR_YELLOW;
            break;
        default:
            color = COLOR_WHITE;
    };

    return color;
}

static void reverseString(char* str)
{
    int start = 0;
    int end = strlen(str) - 1;
    char temp;

    while (start < end) {
        temp = str[start];
        str[start] = str[end];
        str[end] = temp;

        start++;
        end--;
    }
}

static void prepareCounters(char *countersBuf, char *commanderDamageBuf, int buflen, struct GameState *state, int player, int ud)
{
    char tmpBuf[buflen];
    tmpBuf[0] = 0;
    countersBuf[0] = 0;
    commanderDamageBuf[0] = 0;

    if (ud) {
        for (int j = state->maxOpponents - 1, c = state->maxOpponents; j >= 0; j--, c--) {
            char tmp[buflen];
            if (c == player) {
                c--;
            }

            int col = getPlayerColor(c);

            if (state->playerState[player].commanderDamage[j] >= MAX_COMMANDER_DAMAGE) {
                col = COLOR_RED;
            }

            if (j == state->maxOpponents - 1) {
                snprintf(tmp, buflen, "%d%s", state->playerState[player].commanderDamage[j], (player == state->selectedPlayer && j == state->selectedCommanderDamageOrCounter) ? "." : "");
                reverseString(tmp);
                snprintf(commanderDamageBuf, buflen, "#{ci:%d}%s", convertColor(col), tmp);
            } else {
                snprintf(tmp, buflen, "%d%s", state->playerState[player].commanderDamage[j], (player == state->selectedPlayer && j == state->selectedCommanderDamageOrCounter) ? "." : "");
                reverseString(tmp);
                snprintf(commanderDamageBuf, buflen, "%s #{ci:%d}%s", tmpBuf, convertColor(col), tmp);
            }
            strcpy(tmpBuf, commanderDamageBuf);
        }
    } else  {
        for (int j = 0, c = 0; j < state->maxOpponents; j++, c++) {
            if (c == player) {
                c++;
            }

            int col = getPlayerColor(c);

            if (state->playerState[player].commanderDamage[j] >= MAX_COMMANDER_DAMAGE) {
                col = COLOR_RED;
            }

            if (j == 0) {
                snprintf(commanderDamageBuf, buflen, "#{ci:%d}%d%s", convertColor(col), state->playerState[player].commanderDamage[j], (player == state->selectedPlayer && j == state->selectedCommanderDamageOrCounter) ? "." : "");
            } else {
                snprintf(commanderDamageBuf, buflen, "%s #{ci:%d}%d%s", tmpBuf, convertColor(col), state->playerState[player].commanderDamage[j], (player == state->selectedPlayer && j == state->selectedCommanderDamageOrCounter) ? "." : "");
            }
            strcpy(tmpBuf, commanderDamageBuf);
        }
    }

    char tmp1[32], tmp2[32], tmp3[32], tmp4[32];
    snprintf(tmp1, 32, "%d%s", state->playerState[player].poisonCounters, (state->selectedPlayer == player && state->selectedCommanderDamageOrCounter == POISON_COUNTER) ? "." : "");
    snprintf(tmp2, 32, "%d%s", state->playerState[player].energyCounters, (state->selectedPlayer == player && state->selectedCommanderDamageOrCounter == ENERGY_COUNTER) ? "." : "");
    snprintf(tmp3, 32, "%d%s", state->playerState[player].experienceCounters, (state->selectedPlayer == player && state->selectedCommanderDamageOrCounter == EXPERIENCE_COUNTER) ? "." : "");
    if (state->maxOpponents > 0) {
        snprintf(tmp4, 32, "%d%s", state->playerState[player].commanderTaxCounter, (state->selectedPlayer == player && state->selectedCommanderDamageOrCounter == COMMANDERTAX_COUNTER) ? "." : "");
    }
    if (ud) {
        reverseString(tmp1);
        reverseString(tmp2);
        reverseString(tmp3);
        if (state->maxOpponents > 0) {
            reverseString(tmp4);

            snprintf(countersBuf, buflen, "#{ci:%d}%sC #{ci:%d}%sX #{ci:%d}%sE #{ci:%d}%sP",
                               convertColor(COMMANDERTAX_COLOR), tmp4,
                               convertColor(EXPERIENCE_COLOR), tmp3,
                               convertColor(ENERGY_COLOR), tmp2,
                               convertColor(POISON_COLOR), tmp1);
        } else {
            snprintf(countersBuf, buflen, "#{ci:%d}%sX #{ci:%d}%sE #{ci:%d}%sP",
                               convertColor(EXPERIENCE_COLOR), tmp3,
                               convertColor(ENERGY_COLOR), tmp2,
                               convertColor(POISON_COLOR), tmp1);
        }
    } else {
        if (state->maxOpponents > 0) {
            snprintf(countersBuf, buflen, "#{ci:%d}P%s #{ci:%d}E%s #{ci:%d}X%s #{ci:%d}C%s",
                               convertColor(POISON_COLOR), tmp1,
                               convertColor(ENERGY_COLOR), tmp2,
                               convertColor(EXPERIENCE_COLOR), tmp3,
                               convertColor(COMMANDERTAX_COLOR), tmp4);
        } else {
            snprintf(countersBuf, buflen, "#{ci:%d}P%s #{ci:%d}E%s #{ci:%d}X%s",
                               convertColor(POISON_COLOR), tmp1,
                               convertColor(ENERGY_COLOR), tmp2,
                               convertColor(EXPERIENCE_COLOR), tmp3);
        }
    }
}

static void printCounters(int row, int row2, int offset_x, int width_x, int ud, struct GameState *state, int player)
{
    char commanderDamageBuf[256], countersBuf[256];
    prepareCounters(countersBuf, commanderDamageBuf, 256, state, player, ud);

    if (row == row2) {
        strcat(commanderDamageBuf, " ");
        strcat(commanderDamageBuf, countersBuf);
        printText(row, offset_x, width_x, ud, commanderDamageBuf);
    } else {
        printText(row, offset_x, width_x, ud, commanderDamageBuf);
        printText(row2, offset_x, width_x, ud, countersBuf);
    }
}

static void printLifeRegular(struct GameState *state, int player)
{
    char commanderDamageBuf[256], countersBuf[256];
    prepareCounters(commanderDamageBuf, countersBuf, 256, state, player, 0);

    if (state->selectedPlayer == player) {
        printTextColor(1 + player * 2, 10, getScreenWidth(), getPlayerColor(player), 0, "*Player %d: %d %s", player, state->playerState[player].lifeCounter, commanderDamageBuf);
    } else if (state->playerState[player].lifeCounter <= 0 || state->playerState[player].poisonCounters >= MAX_POISON_COUNTERS) {
        printTextColor(1 + player * 2, 10, getScreenWidth(), COLOR_RED, 0, " Player %d: %d %s", player, state->playerState[player].lifeCounter, commanderDamageBuf);
    } else {
        printTextColor(1 + player * 2, 10, getScreenWidth(), getPlayerColor(player), 0, " Player %d: %d %s", player, state->playerState[player].lifeCounter, commanderDamageBuf);
    }

    printText(1 + player * 2 + 1, 10 + getGlyphWidth() * 2, getScreenWidth(), 0, countersBuf);
}

static void getLargeLayout(int player, int maxSquares, int ud, int *offset_x, int *row, int *row2)
{
    if (maxSquares == 2) {
        switch (player) {
            case 0:
                // top left corner:
                *offset_x = 10;
                *row = 7;
                *row2 = 14;
                break;
            case 1:
                // top right corner:
                *offset_x = 130;
                *row = 7;
                *row2 = 14;
                break;
        };
    } else {
        if (ud) {
            switch (player) {
                case 0:
                    // top left corner:
                    *offset_x = 5;
                    *row = 8;
                    *row2 = 1;
                    break;
                case 1:
                    // top right corner:
                    *offset_x = 125;
                    *row = 8;
                    *row2 = 1;
                    break;
            };
        } else {
            switch (player) {
                case 0:
                    // top left corner:
                    *offset_x = 5;
                    *row = 1;
                    *row2 = 8;
                    break;
                case 1:
                    // top right corner:
                    *offset_x = 125;
                    *row = 1;
                    *row2 = 8;
                    break;
                case 2:
                    // bottom left corner:
                    *offset_x = 5;
                    *row = 11;
                    *row2 = 18;
                    break;
                case 3:
                    // bottom right corner:
                    *offset_x = 125;
                    *row = 11;
                    *row2 = 18;
                    break;
            };
        }
    }
}

static void getLargeOffsets(int square, int maxSquares, int ud, int *offset_x, int *offset_y)
{
    if (maxSquares == 2) {
        switch (square) {
            case 0:
                // top left corner:
                *offset_x = 0;
                *offset_y = 10 + (getScreenHeight() - getLargeGlyphHeight()) / 2;
                break;
            case 1:
                // top right corner:
                *offset_x = 4 * getLargeGlyphWidth();
                *offset_y = 10 + (getScreenHeight() - getLargeGlyphHeight()) / 2;
                break;
        };
    } else {
        if (ud) {
            switch (square) {
                case 0:
                    // top left corner:
                    *offset_x = getScreenWidth() - 3 * getLargeGlyphWidth();
                    *offset_y = getScreenHeight() - 10 - getLargeGlyphHeight();
                    break;
                case 1:
                    // top right corner:
                    *offset_x = 1 * getLargeGlyphWidth();
                    *offset_y = getScreenHeight() - 10 - getLargeGlyphHeight();
                    break;
            };
        } else {
            switch (square) {
                case 0:
                    // top left corner:
                    *offset_x = 0;
                    *offset_y = 20;
                    break;
                case 1:
                    // top right corner:
                    *offset_x = 4 * getLargeGlyphWidth();
                    *offset_y = 20;
                    break;
                case 2:
                    // bottom left corner:
                    *offset_x = 0;
                    *offset_y = getScreenHeight() - getLargeGlyphHeight() - 10;
                    break;
                case 3:
                    // bottom right corner:
                    *offset_x = 4 * getLargeGlyphWidth();
                    *offset_y = getScreenHeight() - getLargeGlyphHeight() - 10;
                    break;
            };
        }
    }
}

static void printLifeLarge(struct GameState *state, int player, int ud)
{
    int offset_x = 0;
    int offset_y = 0;

    getLargeOffsets(player, state->maxPlayers, ud, &offset_x, &offset_y);

    if (state->selectedPlayer == player) {
        printLargeNumber(offset_x, offset_y, state->playerState[player].lifeCounter, getPlayerColor(player), ud, 1);
    } else if (state->playerState[player].lifeCounter <= 0 || state->playerState[player].poisonCounters >= MAX_POISON_COUNTERS) {
        printLargeNumber(offset_x, offset_y, state->playerState[player].lifeCounter, COLOR_RED, ud, 0);
    } else {
        printLargeNumber(offset_x, offset_y, state->playerState[player].lifeCounter, getPlayerColor(player), ud, 0);
    }

    if (state->maxOpponents > 0) {
        int row = 0;
        int row2 = 0;

        getLargeLayout(player, state->maxPlayers, ud, &offset_x, &row, &row2);

        printCounters(row, row2, offset_x, getScreenWidth() / 2, ud, state, player);
    } else {
        int row = 0;
        int row2 = 0;

        for (int j = 0; j < state->maxPlayers; j++) {
            getLargeLayout(j, state->maxPlayers, ud, &offset_x, &row, &row2);

            printCounters(row2, row2, offset_x, getScreenWidth() / 2, 0, state, j);
        }
    }
}

static void printLifeHuge(struct GameState *state, int print_commander_damage)
{
    printHugeNumber(state->playerState[0].lifeCounter);

    if (print_commander_damage) {
        printCounters(18, 18, 5, getScreenWidth(), 0, state, 0);
    }
}

static const char *getSongName(struct GameState *state)
{
    switch (state->selectedBackgroundSong) {
        case 0:
            return "AI Renaissance";
        case 1:
            return "Retrospective ";
        case 2:
            return "No music      ";
    };

    return "UNKNOWN SONG";
}

static int handleKeysControls(struct GameState *state, int keys_pressed, int keys_released)
{
    int stateChanged = state->previousState != state->state;

    // any button returns?
    if (keys_released) {
        clearScreen();

        return state->stateToReturnTo;
    }

    if (stateChanged) {
        printTextColor(1, 10, getScreenWidth(), COLOR_GREEN, 0, "Controls:");
        printTextColor(2, 10, getScreenWidth(), COLOR_WHITE, 0, "SELECT to select player.");
        printTextColor(3, 10, getScreenWidth(), COLOR_WHITE, 0, "U/D to change life.");
        printTextColor(4, 10, getScreenWidth(), COLOR_WHITE, 0, "SL,SR to select counter.");
        printTextColor(5, 10, getScreenWidth(), COLOR_WHITE, 0, "L/R to change counter.");
        printTextColor(6, 10, getScreenWidth(), COLOR_WHITE, 0, "START to enter menu.");
        printTextColor(8, 10, getScreenWidth(), COLOR_WHITE, 0, "Colored numbers near the");
        printTextColor(9, 10, getScreenWidth(), COLOR_WHITE, 0, "life total indicate the");
        printTextColor(10, 10, getScreenWidth(), COLOR_WHITE, 0, "Commander Damage or:");
        printTextColor(11, 10, getScreenWidth(), COLOR_WHITE, 0, "P = Poison");
        printTextColor(12, 10, getScreenWidth(), COLOR_WHITE, 0, "E = Engery");
        printTextColor(13, 10, getScreenWidth(), COLOR_WHITE, 0, "X = Experience");
        printTextColor(14, 10, getScreenWidth(), COLOR_WHITE, 0, "C = Commander Tax");
        printTextColor(15, 10, getScreenWidth(), COLOR_WHITE, 0, "A dot(.) indicates the");
        printTextColor(16, 10, getScreenWidth(), COLOR_WHITE, 0, "current selection.");
        printTextColor(18, 10, getScreenWidth(), COLOR_WHITE, 0, "Press any button to leave");
        printTextColor(19, 10, getScreenWidth(), COLOR_WHITE, 0, "this menu.");
    }

    return state->state;
}

static int handleKeysSetup(struct GameState *state, int keys_pressed, int keys_released)
{
    int stateChanged = state->previousState != state->state;
    int selectedSetupItemChanged = stateChanged;
    int maxPlayersChanged = stateChanged;

    if (keys_released & KEY_START || keys_released & KEY_A) {
        if (state->selectedSetupItem == SETUP_ITEM_QUICK_START_COMMANDER4P) {
            // reset the screen on transition
            clearScreen();

            state->maxPlayers = 4;
            state->maxOpponents = 3;
            state->startingLife = 40;

            initializeStartingLifeAndCounters(state);

            return STATE_COUNTLIFE;
        } else if (state->selectedSetupItem == SETUP_ITEM_QUICK_START_COMMANDER1P) {
            // reset the screen on transition
            clearScreen();

            state->maxPlayers = 1;
            state->maxOpponents = 3;
            state->startingLife = 40;

            initializeStartingLifeAndCounters(state);

            return STATE_COUNTLIFE;
        } else if (state->selectedSetupItem == SETUP_ITEM_QUICK_START_1V1) {
            // reset the screen on transition
            clearScreen();

            state->maxPlayers = 2;
            state->maxOpponents = 0;
            state->startingLife = 20;
            state->selectedCommanderDamageOrCounter = FIRST_COUNTER;

            initializeStartingLifeAndCounters(state);

            return STATE_COUNTLIFE;
        } else if (state->selectedSetupItem == SETUP_ITEM_START) {
            // reset the screen on transition
            clearScreen();

            if (state->maxPlayers != 1) {
                state->maxOpponents = state->maxPlayers - 1;
            }

            initializeStartingLifeAndCounters(state);

            return STATE_COUNTLIFE;
        } else if (state->selectedSetupItem == SETUP_ITEM_LOAD_SAVE) {
            if (flashValid(0)) {
                clearScreen();

                loadState(state, 0);
                adjustBackgroundSong(state);

                return STATE_COUNTLIFE;
            } else {
                printTextColor(17, 10, getScreenWidth(), COLOR_RED, 0, "NO SAVE FOUND");
            }
        } else if (state->selectedSetupItem == SETUP_ITEM_LOAD_AUTOSAVE) {
            if (flashValid(1)) {
                clearScreen();

                loadState(state, 1);
                adjustBackgroundSong(state);

                return STATE_COUNTLIFE;
            } else {
                printTextColor(17, 10, getScreenWidth(), COLOR_RED, 0, "NO SAVE FOUND");
            }
        } else if (state->selectedSetupItem == SETUP_ITEM_CONTROLS) {
            // reset the screen on transition
            clearScreen();

            state->stateToReturnTo = STATE_SETUP;

            return STATE_CONTROLS;
        }
    }

    if (keys_released & KEY_UP) {
        if (state->selectedSetupItem > 0) {
            state->selectedSetupItem--;
            if (state->selectedSetupItem == SETUP_ITEM_OPPONENTS && state->maxPlayers != 1) {
                state->selectedSetupItem--;
            }
            selectedSetupItemChanged = 1;
        }
    }

    if (keys_released & KEY_DOWN) {
        if (state->selectedSetupItem < SETUP_ITEMS - 1) {
            state->selectedSetupItem++;
            if (state->selectedSetupItem == SETUP_ITEM_OPPONENTS && state->maxPlayers != 1) {
                state->selectedSetupItem++;
            }
            selectedSetupItemChanged = 1;
        }
    }

    if (state->selectedSetupItem == SETUP_ITEM_STARTING_LIFE) {
        if (keys_released & KEY_LEFT) {
            state->startingLife -= 5;
            selectedSetupItemChanged = 1;
        }

        if (keys_released & KEY_RIGHT) {
            state->startingLife += 5;
            selectedSetupItemChanged = 1;
        }
    }

    if (state->selectedSetupItem == SETUP_ITEM_SONG) {
        int selectedBackgroundSongBefore = state->selectedBackgroundSong;

        if (keys_released & KEY_LEFT) {
            if (state->selectedBackgroundSong > 0) {
                state->selectedBackgroundSong--;
                selectedSetupItemChanged = 1;
            }
        }

        if (keys_released & KEY_RIGHT) {
            if (state->selectedBackgroundSong < MAX_BACKGROUND_SONGS - 1) {
                state->selectedBackgroundSong++;
                selectedSetupItemChanged = 1;
            }
        }

        if (state->selectedBackgroundSong != selectedBackgroundSongBefore) {
            adjustBackgroundSong(state);
        }
    }

    if (state->selectedSetupItem == SETUP_ITEM_SFX) {
        if (keys_released & KEY_LEFT || keys_released & KEY_RIGHT) {
            state->sfxEnabled = state->sfxEnabled ? 0 : 1;
        }
    }

    if (state->selectedSetupItem == SETUP_ITEM_PLAYERS) {
        if (keys_released & KEY_LEFT) {
            if (state->maxPlayers > 1) {
                state->maxPlayers--;
                selectedSetupItemChanged = 1;
                maxPlayersChanged = 1;
                if (state->maxPlayers == 1) {
                    state->maxOpponents = 3;
                }
            }
        }

        if (keys_released & KEY_RIGHT) {
            if (state->maxPlayers < MAX_PLAYERS) {
                state->maxPlayers++;
                selectedSetupItemChanged = 1;
                maxPlayersChanged = 1;
            }
        }
    }

    if (state->selectedSetupItem == SETUP_ITEM_OPPONENTS) {
        if (keys_released & KEY_LEFT) {
            if (state->maxOpponents > 0) {
                state->maxOpponents--;
                selectedSetupItemChanged = 1;
            }
        }

        if (keys_released & KEY_RIGHT) {
            if (state->maxOpponents < MAX_PLAYERS - 1) {
                state->maxOpponents++;
                selectedSetupItemChanged = 1;
            }
        }
    }

    if (stateChanged) {
        printTextColor(1, 10, getScreenWidth(), COLOR_GREEN, 0, "MagicBoy Advance!");
    }

    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_QUICK_START_COMMANDER4P)
        printTextColor(2, 10, getScreenWidth(), COLOR_WHITE, 0, "%cCommander 4p", (state->selectedSetupItem == SETUP_ITEM_QUICK_START_COMMANDER4P) ? '*' : ' ');
    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_QUICK_START_COMMANDER1P)
        printTextColor(3, 10, getScreenWidth(), COLOR_WHITE, 0, "%cCommander 1p (3 opponents)", (state->selectedSetupItem == SETUP_ITEM_QUICK_START_COMMANDER1P) ? '*' : ' ');
    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_QUICK_START_1V1)
        printTextColor(4, 10, getScreenWidth(), COLOR_WHITE, 0, "%c1v1", (state->selectedSetupItem == SETUP_ITEM_QUICK_START_1V1) ? '*' : ' ');
    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_STARTING_LIFE)
        printTextColor(6, 10, getScreenWidth(), COLOR_WHITE, 0, "%c%d starting life", (state->selectedSetupItem == SETUP_ITEM_STARTING_LIFE) ? '*' : ' ', state->startingLife);
    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_PLAYERS)
        printTextColor(7, 10, getScreenWidth(), COLOR_WHITE, 0, "%c%d players", (state->selectedSetupItem == SETUP_ITEM_PLAYERS) ? '*' : ' ', state->maxPlayers);
    if (state->maxPlayers == 1) {
        if (stateChanged || selectedSetupItemChanged || maxPlayersChanged || state->selectedSetupItem == SETUP_ITEM_OPPONENTS)
            printTextColor(8, 10, getScreenWidth(), COLOR_WHITE, 0, "%c%d opponents (Commander).", (state->selectedSetupItem == SETUP_ITEM_OPPONENTS) ? '*' : ' ', state->maxOpponents);
    } else {
        if (stateChanged || selectedSetupItemChanged || maxPlayersChanged || state->selectedSetupItem == SETUP_ITEM_OPPONENTS)
            printTextColor(8, 10, getScreenWidth(), COLOR_WHITE, 0, "");
    }

    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_SONG)
        printTextColor(9, 10, getScreenWidth(), COLOR_WHITE, 0, "%cSong: %s", (state->selectedSetupItem == SETUP_ITEM_SONG) ? '*' : ' ', getSongName(state));
    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_SFX)
        printTextColor(10, 10, getScreenWidth(), COLOR_WHITE, 0, "%cSound effects: %s", (state->selectedSetupItem == SETUP_ITEM_SFX) ? '*' : ' ', state->sfxEnabled ? "Yes" : "No");
    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_START)
        printTextColor(11, 10, getScreenWidth(), COLOR_WHITE, 0, "%cStart", (state->selectedSetupItem == SETUP_ITEM_START) ? '*' : ' ');
    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_LOAD_SAVE)
        printTextColor(13, 10, getScreenWidth(), COLOR_WHITE, 0, "%cLoad save", (state->selectedSetupItem == SETUP_ITEM_LOAD_SAVE) ? '*' : ' ');
    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_LOAD_AUTOSAVE)
        printTextColor(14, 10, getScreenWidth(), COLOR_WHITE, 0, "%cLoad autosave", (state->selectedSetupItem == SETUP_ITEM_LOAD_AUTOSAVE) ? '*' : ' ');
    if (stateChanged || selectedSetupItemChanged || state->selectedSetupItem == SETUP_ITEM_CONTROLS)
        printTextColor(16, 10, getScreenWidth(), COLOR_WHITE, 0, "%cShow controls.", (state->selectedSetupItem == SETUP_ITEM_CONTROLS) ? '*' : ' ');

    return state->state;
}

static void handleDecreaseSelectedCommanderDamage(struct GameState *state, int *changed, int *selectedCommanderDamageChanged)
{
    if (state->selectedCommanderDamageOrCounter == FIRST_COUNTER && state->maxOpponents > 0) {
        state->selectedCommanderDamageOrCounter = state->maxOpponents - 1;
        *changed = 1;
        *selectedCommanderDamageChanged = 1;
    } else if (state->selectedCommanderDamageOrCounter > 0 && state->maxOpponents > 0) {
        state->selectedCommanderDamageOrCounter--;
        *changed = 1;
        *selectedCommanderDamageChanged = 1;
    } else if (state->selectedCommanderDamageOrCounter > FIRST_COUNTER) {
        state->selectedCommanderDamageOrCounter--;
        *changed = 1;
        *selectedCommanderDamageChanged = 1;
    }
}

static void handleIncreaseSelectedCommanderDamage(struct GameState *state, int *changed, int *selectedCommanderDamageChanged)
{
    if (state->selectedCommanderDamageOrCounter < state->maxOpponents - 1) {
        state->selectedCommanderDamageOrCounter++;
        *changed = 1;
        *selectedCommanderDamageChanged = 1;
    } else if (state->selectedCommanderDamageOrCounter == state->maxOpponents - 1) {
        state->selectedCommanderDamageOrCounter = FIRST_COUNTER;
        *changed = 1;
        *selectedCommanderDamageChanged = 1;
    } else if (state->selectedCommanderDamageOrCounter < state->lastCounter) {
        state->selectedCommanderDamageOrCounter++;
        *changed = 1;
        *selectedCommanderDamageChanged = 1;
    }
}

static void printLifeChanged(struct GameState *state, int clear)
{
    if (state->printedRegular) {
        if (clear) {
            printTextColor(18, getScreenWidth() - getGlyphWidth() * 5, getGlyphWidth() * 5, getPlayerColor(state->selectedPlayer), 0, "     ");
        } else {
            printTextColor(18, getScreenWidth() - getGlyphWidth() * 5, getGlyphWidth() * 5, getPlayerColor(state->selectedPlayer), 0, "%s%d", (state->lifeChangedCurrent > 0) ? "+" : "", state->lifeChangedCurrent);
        }
    } else {
        if (state->maxPlayers == 1) {
            if (clear) {
                printTextColor(15, (getScreenWidth() * 4) / 5 - getGlyphWidth() * 2, getGlyphWidth() * 5, getPlayerColor(state->selectedPlayer), 0, "     ");
            } else {
                printTextColor(15, (getScreenWidth() * 4) / 5 - getGlyphWidth() * 2, getGlyphWidth() * 5, getPlayerColor(state->selectedPlayer), 0, "%s%d", (state->lifeChangedCurrent > 0) ? "+" : "", state->lifeChangedCurrent);
            }
        } else if (state->maxPlayers > 4) {
            if (clear) {
                printTextColor(18, getScreenWidth() - getGlyphWidth() * 5, getGlyphWidth() * 5, getPlayerColor(state->selectedPlayer), 0, "     ");
            } else {
                printTextColor(18, getScreenWidth() - getGlyphWidth() * 5, getGlyphWidth() * 5, getPlayerColor(state->selectedPlayer), 0, "%s%d", (state->lifeChangedCurrent > 0) ? "+" : "", state->lifeChangedCurrent);
            }
        } else {
            if (clear) {
                printTextColor(9, getScreenWidth() / 2 - getGlyphWidth() * 2, getGlyphWidth() * 5, getPlayerColor(state->selectedPlayer), 0, "     ");
            } else {
                printTextColor(9, getScreenWidth() / 2 - getGlyphWidth() * 2, getGlyphWidth() * 5, getPlayerColor(state->selectedPlayer), 0, "%s%d", (state->lifeChangedCurrent > 0) ? "+" : "", state->lifeChangedCurrent);
            }
        }
    }
}

static int shouldPrintPlayerRegular(struct GameState *state, int player)
{
    return (state->playerState[player].lifeCounter < MIN_LIFE_FOR_CUSTOM_PRINT || state->playerState[player].lifeCounter > MAX_LIFE_FOR_CUSTOM_PRINT) ? 1 : 0;
}

static int shouldPrintRegular(struct GameState *state)
{
    for (int i = 0; i < state->maxPlayers; i++) {
        if (shouldPrintPlayerRegular(state, i)) {
            return 1;
        }
    }

    return 0;
}

static int handleKeysCountLife(struct GameState *state, int keys_pressed, int keys_released)
{
    int stateChanged = state->previousState != state->state;
    int changed = stateChanged;
    int lifeBefore, selectedPlayerBefore;
    int lifeChanged = stateChanged;
    int poisonBefore = 0;
    int commanderDamageOrCounterChanged = stateChanged;
    int selectedCommanderDamageChanged = stateChanged;
    int selectedPlayerChanged = stateChanged;
    int poisonCountersChanged = stateChanged, energyCountersChanged = stateChanged, experienceCountersChanged = stateChanged, commanderTaxCounterChanged = stateChanged;
    int skipLifeChanged = 0;

    int keyIncreaseSelectedCommanderDamage = KEY_R;
    int keyDecreaseSelectedCommanderDamage = KEY_L;
    int keyIncreaseCommanderDamageOrCounter = KEY_RIGHT;
    int keyDecreaseCommanderDamageOrCounter = KEY_LEFT;
    int keyIncreaseLife = KEY_UP;
    int keyDecreaseLife = KEY_DOWN;

    if (state->upsideDownNumbers && state->selectedPlayer < 2) {
        int willPrintRegular = shouldPrintRegular(state);

        if (!willPrintRegular) {
            keyIncreaseSelectedCommanderDamage = KEY_L;
            keyDecreaseSelectedCommanderDamage = KEY_R;
            keyIncreaseCommanderDamageOrCounter = KEY_LEFT;
            keyDecreaseCommanderDamageOrCounter = KEY_RIGHT;
            keyIncreaseLife = KEY_DOWN;
            keyDecreaseLife = KEY_UP;
        }
    }

    if (keys_released & KEY_START) {
        // reset the screen on transition
        clearScreen();

        state->selectedMenuItem = 0;
        return STATE_MENU;
    }

    selectedPlayerBefore = state->selectedPlayer;

    state->keysDown |= keys_pressed;
    state->keysDown &= ~keys_released;

    if (keys_released & keyIncreaseSelectedCommanderDamage) {
        handleIncreaseSelectedCommanderDamage(state, &changed, &selectedCommanderDamageChanged);
    }

    if (keys_released & keyDecreaseSelectedCommanderDamage) {
        handleDecreaseSelectedCommanderDamage(state, &changed, &selectedCommanderDamageChanged);
    }

    if (keys_released & KEY_SELECT) {
        state->selectedPlayer++;
        if (state->selectedPlayer >= state->maxPlayers) {
            state->selectedPlayer = 0;
        }
    }

    lifeBefore = state->playerState[state->selectedPlayer].lifeCounter;

    if (state->keysDown & keyIncreaseLife) {
        state->framesSinceUPPressedOrQuarterSecond++;
    } else {
        state->framesSinceUPPressedOrQuarterSecond = 0;
    }

    if (state->keysDown & keyDecreaseLife) {
        state->framesSinceDOWNPressedOrQuarterSecond++;
    } else {
        state->framesSinceDOWNPressedOrQuarterSecond = 0;
    }

    if (state->framesSinceUPPressedOrQuarterSecond >= FPS / 4) {
        state->playerState[state->selectedPlayer].lifeCounter += 5;
        state->lifeChangedCurrent += 5;
        state->triggerClearLifeChangedCurrentInFrames = TIME_CLEAR_LIFE_CHANGED;
        state->framesSinceUPPressedOrQuarterSecond = 0;
        lifeChanged = 1;
    }

    if (state->framesSinceDOWNPressedOrQuarterSecond >= FPS / 4) {
        state->playerState[state->selectedPlayer].lifeCounter -= 5;
        state->lifeChangedCurrent -= 5;
        state->triggerClearLifeChangedCurrentInFrames = TIME_CLEAR_LIFE_CHANGED;
        state->framesSinceDOWNPressedOrQuarterSecond = 0;
        lifeChanged = 1;
    }

    if (keys_released & keyIncreaseLife) {
        state->playerState[state->selectedPlayer].lifeCounter++;
        state->lifeChangedCurrent++;
        state->triggerClearLifeChangedCurrentInFrames = TIME_CLEAR_LIFE_CHANGED;
        lifeChanged = 1;
    }

    if (keys_released & keyDecreaseLife) {
        state->playerState[state->selectedPlayer].lifeCounter--;
        state->lifeChangedCurrent--;
        state->triggerClearLifeChangedCurrentInFrames = TIME_CLEAR_LIFE_CHANGED;
        lifeChanged = 1;
    }

    if (state->selectedCommanderDamageOrCounter < FIRST_COUNTER) {
        if (keys_released & keyDecreaseCommanderDamageOrCounter) {
            if (state->playerState[state->selectedPlayer].commanderDamage[state->selectedCommanderDamageOrCounter] > 0) {
                state->playerState[state->selectedPlayer].commanderDamage[state->selectedCommanderDamageOrCounter]--;
                state->playerState[state->selectedPlayer].lifeCounter++;
                // intentionally not incrementing lifeChangedCurrent because it might be confusing.
                commanderDamageOrCounterChanged = 1;
                changed = 1;
            }
        }

        if (keys_released & keyIncreaseCommanderDamageOrCounter) {
            state->playerState[state->selectedPlayer].commanderDamage[state->selectedCommanderDamageOrCounter]++;
            state->playerState[state->selectedPlayer].lifeCounter--;
            // intentionally not decrementing lifeChangedCurrent because it might be confusing.
            commanderDamageOrCounterChanged = 1;
            changed = 1;
        }
    } else {
        if (keys_released & keyDecreaseCommanderDamageOrCounter) {
            switch (state->selectedCommanderDamageOrCounter) {
                case POISON_COUNTER:
                    if (state->playerState[state->selectedPlayer].poisonCounters > 0) {
                        poisonBefore = state->playerState[state->selectedPlayer].poisonCounters;
                        state->playerState[state->selectedPlayer].poisonCounters--;
                        poisonCountersChanged = 1;
                    }
                    break;
                case ENERGY_COUNTER:
                    if (state->playerState[state->selectedPlayer].energyCounters > 0) {
                        state->playerState[state->selectedPlayer].energyCounters--;
                        energyCountersChanged = 1;
                    }
                    break;
                case EXPERIENCE_COUNTER:
                    if (state->playerState[state->selectedPlayer].experienceCounters > 0) {
                        state->playerState[state->selectedPlayer].experienceCounters--;
                        experienceCountersChanged = 1;
                    }
                case COMMANDERTAX_COUNTER:
                    if (state->playerState[state->selectedPlayer].commanderTaxCounter > 0) {
                        state->playerState[state->selectedPlayer].commanderTaxCounter -= 2;
                        commanderTaxCounterChanged = 1;
                    }
                    break;
                default:
                    printTextColor(12, 10, getScreenWidth(), COLOR_RED, 0, "UNKNOWN COUNTER");
            };
            commanderDamageOrCounterChanged = 1;
            changed = 1;
        }

        if (keys_released & keyIncreaseCommanderDamageOrCounter) {
            switch (state->selectedCommanderDamageOrCounter) {
                case POISON_COUNTER:
                    poisonBefore = state->playerState[state->selectedPlayer].poisonCounters;
                    state->playerState[state->selectedPlayer].poisonCounters++;
                    poisonCountersChanged = 1;
                    break;
                case ENERGY_COUNTER:
                    state->playerState[state->selectedPlayer].energyCounters++;
                    energyCountersChanged = 1;
                    break;
                case EXPERIENCE_COUNTER:
                    state->playerState[state->selectedPlayer].experienceCounters++;
                    experienceCountersChanged = 1;
                    break;
                case COMMANDERTAX_COUNTER:
                    state->playerState[state->selectedPlayer].commanderTaxCounter += 2;
                    commanderTaxCounterChanged = 1;
                    break;
                default:
                    printTextColor(12, 10, getScreenWidth(), COLOR_RED, 0, "UNKNOWN COUNTER");
            };
            commanderDamageOrCounterChanged = 1;
            changed = 1;
        }
    }

    if (selectedPlayerBefore != state->selectedPlayer) {
        changed = 1;
        selectedPlayerChanged = 1;
        skipLifeChanged = 1;

        state->triggerClearLifeChangedCurrentInFrames = 1;
        state->lifeChangedCurrent = 0;
    } else if (lifeBefore != state->playerState[state->selectedPlayer].lifeCounter) {
        changed = 1;
        if (state->sfxEnabled) {
            if (lifeBefore > 0 && state->playerState[state->selectedPlayer].lifeCounter <= 0) {
                AAS_SFX_Play(0, 64, 16000, AAS_DATA_SFX_START_death,
                             AAS_DATA_SFX_END_death, NULL);
            } else if (lifeBefore < state->playerState[state->selectedPlayer].lifeCounter) {
                AAS_SFX_Play(0, 64, 16000, AAS_DATA_SFX_START_ding,
                             AAS_DATA_SFX_END_ding, NULL);
            } else if (state->playerState[state->selectedPlayer].lifeCounter > 0) {
                AAS_SFX_Play(0, 64, 16000, AAS_DATA_SFX_START_hit,
                             AAS_DATA_SFX_END_hit, NULL);
            }
        }
    } else if (poisonCountersChanged && !stateChanged) {
        if (state->sfxEnabled) {
            if (state->playerState[state->selectedPlayer].poisonCounters >= 10 && poisonBefore < 10) {
                AAS_SFX_Play(0, 64, 16000, AAS_DATA_SFX_START_death,
                             AAS_DATA_SFX_END_death, NULL);
            } else {
                AAS_SFX_Play(0, 64, 16000, AAS_DATA_SFX_START_poison,
                             AAS_DATA_SFX_END_poison, NULL);
            }
        }
    } else if (energyCountersChanged && !stateChanged) {
        if (state->sfxEnabled) {
            AAS_SFX_Play(0, 64, 16000, AAS_DATA_SFX_START_energy,
                         AAS_DATA_SFX_END_energy, NULL);
        }
    } else if (experienceCountersChanged && !stateChanged) {
        if (state->sfxEnabled) {
            AAS_SFX_Play(0, 64, 16000, AAS_DATA_SFX_START_experience,
                         AAS_DATA_SFX_END_experience, NULL);
        }
    }
    // commander tax sound?

    if (changed) {
        if (state->maxPlayers == 1) {
            if (shouldPrintPlayerRegular(state, 0)) {
                if (!state->printedRegular) {
                    clearScreen();
                }
                state->printedRegular = 1;

                printLifeRegular(state, 0);
            } else {
                int screenCleared = 0;
                if (state->printedRegular || (lifeBefore < 0 && state->playerState[0].lifeCounter >= 0)) {
                    clearScreen();
                    screenCleared = 1;
                }
                state->printedRegular = 0;

                printLifeHuge(state, screenCleared || commanderDamageOrCounterChanged || selectedCommanderDamageChanged || poisonCountersChanged || energyCountersChanged || experienceCountersChanged || commanderTaxCounterChanged);
            }
        } else if (state->maxPlayers <= 4) {
            int printRegular = shouldPrintRegular(state);

            int screenCleared = 0;
            if (state->printedRegular != printRegular) {
                clearScreen();
                screenCleared = 1;
            }

            state->printedRegular = printRegular;

            for (int i = 0; i < state->maxPlayers; i++) {
                if (!selectedPlayerChanged && !screenCleared && i != state->selectedPlayer) {
                    continue;
                }

                if (printRegular) {
                    printLifeRegular(state, i);
                } else {
                    int ud = 0;
                    if (i < 2 && state->upsideDownNumbers) {
                        ud = 1;
                    }
                    printLifeLarge(state, i, ud);
                }
            }
        } else {
            if (selectedPlayerChanged && !stateChanged) {
                printLifeRegular(state, selectedPlayerBefore);
                printLifeRegular(state, state->selectedPlayer);
            } else if (stateChanged) {
                for (int i = 0; i < state->maxPlayers; i++) {
                    printLifeRegular(state, i);
                }
            } else if (lifeChanged || commanderDamageOrCounterChanged || selectedCommanderDamageChanged || poisonCountersChanged || energyCountersChanged || experienceCountersChanged || commanderTaxCounterChanged) {
                printLifeRegular(state, state->selectedPlayer);
            }

            state->printedRegular = 1;
        }

        if (!skipLifeChanged && state->triggerClearLifeChangedCurrentInFrames > 0) {
            printLifeChanged(state, 0);
        }


        if (!stateChanged && (lifeChanged || commanderDamageOrCounterChanged)) {
            // autosave
            state->triggerAutoSaveInFrames = TIME_AUTO_SAVE;
        }
    }

    if (state->triggerClearLifeChangedCurrentInFrames > 0) {
        state->triggerClearLifeChangedCurrentInFrames--;
        if (state->triggerClearLifeChangedCurrentInFrames == 0) {
            state->lifeChangedCurrent = 0;
            printLifeChanged(state, 1);
        }
    }

    if (state->triggerAutoSaveInFrames > 0) {
        state->triggerAutoSaveInFrames--;
        if (state->triggerAutoSaveInFrames == 0) {
            // autosave
            saveState(state, 1);
            printTextColor(19, 10, getScreenWidth(), COLOR_WHITE, 0, "Saved!");
        } else if (state->triggerAutoSaveInFrames % 60 == 0) {
            printTextColor(19, 10, getScreenWidth(), COLOR_WHITE, 0, "Saving in %d seconds.", (state->triggerAutoSaveInFrames / FPS) + 1);
        }
    }

    return state->state;
}

static int handleKeysMenu(struct GameState *state, int keys_pressed, int keys_released)
{
    int stateChanged = state->previousState != state->state;
    int selectedMenuItemChanged = stateChanged;
    int songChanged = stateChanged;
    int sfxChanged = stateChanged;

    if (keys_released & KEY_START || keys_released & KEY_B) {
        // reset the screen on transition
        clearScreen();

        return STATE_COUNTLIFE;
    }

    if (keys_released & KEY_A) {
        if (state->selectedMenuItem == MENU_ITEM_QUIT) {
            // reset the screen on transition
            clearScreen();

            initializeGameState(state);

            return STATE_SETUP;
        } else if (state->selectedMenuItem == MENU_ITEM_SAVE || state->selectedMenuItem == MENU_ITEM_SAVE_AND_QUIT) {
            // reset the screen on transition
            clearScreen();

            // save
            saveState(state, 0);

            if (state->selectedMenuItem == MENU_ITEM_SAVE_AND_QUIT) {
                initializeGameState(state);

                return STATE_SETUP;
            }

            return STATE_COUNTLIFE;
        } else if (state->selectedMenuItem == MENU_ITEM_RETURN) {
            // reset the screen on transition
            clearScreen();

            return STATE_COUNTLIFE;
        } else if (state->selectedMenuItem == MENU_ITEM_FLIP_TOP_NUMBERS) {
            // reset the screen on transition
            clearScreen();

            state->upsideDownNumbers = state->upsideDownNumbers ? 0 : 1;
            return STATE_COUNTLIFE;
        } else if (state->selectedMenuItem == MENU_ITEM_CONTROLS) {
            // reset the screen on transition
            clearScreen();

            state->stateToReturnTo = STATE_MENU;

            return STATE_CONTROLS;
        }
    }

    if (keys_released & KEY_UP) {
        if (state->selectedMenuItem > 0) {
            if (!(state->maxPlayers > 2 && state->maxPlayers < 5) && state->selectedMenuItem == MENU_ITEM_SONG) {
                state->selectedMenuItem--;
            }
            state->selectedMenuItem--;
            selectedMenuItemChanged = 1;
        }
    }

    if (keys_released & KEY_DOWN) {
        if (state->selectedMenuItem < MENU_ITEMS - 1) {
            if (!(state->maxPlayers > 2 && state->maxPlayers < 5) && state->selectedMenuItem == MENU_ITEM_RETURN) {
                state->selectedMenuItem++;
            }
            state->selectedMenuItem++;
            selectedMenuItemChanged = 1;
        }
    }

    if (keys_released & KEY_LEFT) {
        if (state->selectedMenuItem == MENU_ITEM_SONG) {
            if (state->selectedBackgroundSong > 0) {
                state->selectedBackgroundSong--;
                adjustBackgroundSong(state);
                songChanged = 1;
            }
        } else if (state->selectedMenuItem == MENU_ITEM_SFX) {
            state->sfxEnabled = state->sfxEnabled ? 0 : 1;
            sfxChanged = 1;
        }
    }

    if (keys_released & KEY_RIGHT) {
        if (state->selectedMenuItem == MENU_ITEM_SONG) {
            if (state->selectedBackgroundSong < MAX_BACKGROUND_SONGS - 1) {
                state->selectedBackgroundSong++;
                adjustBackgroundSong(state);
                songChanged = 1;
            }
        } else if (state->selectedMenuItem == MENU_ITEM_SFX) {
            state->sfxEnabled = state->sfxEnabled ? 0 : 1;
            sfxChanged = 1;
        }
    }

    if (sfxChanged || selectedMenuItemChanged || songChanged) {
        int row = 1;
        printTextColor(row++, 0, getScreenWidth(), COLOR_GREEN, 0, " Menu");
        printTextColor(row++, 0, getScreenWidth(), COLOR_WHITE, 0, "%cSave.", (state->selectedMenuItem == MENU_ITEM_SAVE) ? '*' : ' ');
        printTextColor(row++, 0, getScreenWidth(), COLOR_WHITE, 0, "%cSave and Quit.", (state->selectedMenuItem == MENU_ITEM_SAVE_AND_QUIT) ? '*' : ' ');
        printTextColor(row++, 0, getScreenWidth(), COLOR_WHITE, 0, "%cReturn to game.", (state->selectedMenuItem == MENU_ITEM_RETURN) ? '*' : ' ');
        if (state->maxPlayers > 2 && state->maxPlayers < 5) {
            printTextColor(row++, 0, getScreenWidth(), COLOR_WHITE, 0, "%cFlip top numbers.", (state->selectedMenuItem == MENU_ITEM_FLIP_TOP_NUMBERS) ? '*' : ' ');
        }
        printTextColor(row++, 0, getScreenWidth(), COLOR_WHITE, 0, "%cSong: %s", (state->selectedMenuItem == MENU_ITEM_SONG) ? '*' : ' ', getSongName(state));
        printTextColor(row++, 0, getScreenWidth(), COLOR_WHITE, 0, "%cSound effects: %s", (state->selectedMenuItem == MENU_ITEM_SFX) ? '*' : ' ', state->sfxEnabled ? "Yes" : "No");
        printTextColor(row++, 0, getScreenWidth(), COLOR_WHITE, 0, "%cShow controls.", (state->selectedMenuItem == MENU_ITEM_CONTROLS) ? '*' : ' ');
        printTextColor(row++, 0, getScreenWidth(), COLOR_WHITE, 0, "%cQuit.", (state->selectedMenuItem == MENU_ITEM_QUIT) ? '*' : ' ');
    }

    return state->state;
}

int main(void)
{
    struct GameState gameState;
    initializeGameState(&gameState);

    // This isn't initialized in initializeGameState so that it won't get
    // reset by accident while a different song is playing.
    gameState.selectedBackgroundSong = 0;

    AAS_SetConfig(AAS_CONFIG_MIX_32KHZ, AAS_CONFIG_CHANS_8,
                  AAS_CONFIG_SPATIAL_STEREO, AAS_CONFIG_DYNAMIC_OFF);

    irqInit();
    irqSet(IRQ_TIMER1, AAS_Timer1InterruptHandler);
    irqEnable(IRQ_VBLANK);

    // TODO: make configureable?
    AAS_MOD_SetVolume(48);
    AAS_SFX_SetVolume(0, 255);
    AAS_SFX_SetVolume(1, 255);

    AAS_MOD_Play(AAS_DATA_MOD_drozerix___ai_renaissance);

    initializeText();
    initializeHugeNumbers();
    initializeLargeNumbers();

    while (1) {
        int keys_pressed, keys_released;

        VBlankIntrWait();

        scanKeys();

        keys_pressed = keysDown();
        keys_released = keysUp();

        int previousState = gameState.state;

        switch (gameState.state) {
            case STATE_SETUP:
                gameState.state = handleKeysSetup(&gameState, keys_pressed, keys_released);
                break;
            case STATE_COUNTLIFE:
                gameState.state = handleKeysCountLife(&gameState, keys_pressed, keys_released);
                break;
            case STATE_MENU:
                gameState.state = handleKeysMenu(&gameState, keys_pressed, keys_released);
                break;
            case STATE_CONTROLS:
                gameState.state = handleKeysControls(&gameState, keys_pressed, keys_released);
                break;
        };

        gameState.previousState = previousState;
    }
}



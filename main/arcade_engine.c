#include "arcade_engine.h"
#include "arcade_lightsout.h"
#include "oled_display.h"
#include "button_handler.h"
#include "config.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int masterState = 0; // 0: Game Selection List, 1: Playing Game
static int masterMenuOption = 0;
static int currentGameIndex = -1;

typedef struct {
    const char* name;
    void (*init_cb)(void);
    void (*update_cb)(button_event_t evt);
} game_entry_t;

static const game_entry_t gamesRegistry[] = {
    {"Lights Out", arcade_lightsout_init, arcade_lightsout_update}
};
static const int registeredGamesCount = sizeof(gamesRegistry)/sizeof(gamesRegistry[0]);

static int sortedIndices[16];

static void sort_games(void) {
    for (int i = 0; i < registeredGamesCount; i++) {
        sortedIndices[i] = i;
    }
    for (int i = 0; i < registeredGamesCount - 1; i++) {
        for (int j = i + 1; j < registeredGamesCount; j++) {
            if (strcasecmp(gamesRegistry[sortedIndices[i]].name, gamesRegistry[sortedIndices[j]].name) > 0) {
                int temp = sortedIndices[i];
                sortedIndices[i] = sortedIndices[j];
                sortedIndices[j] = temp;
            }
        }
    }
}

void arcade_return_to_menu(void) {
    masterState = 0;
    currentGameIndex = -1;
}

void arcade_init(void) {
    masterState = 0;
    masterMenuOption = 0;
    currentGameIndex = -1;
    sort_games();
}

void arcade_update(button_event_t evt) {
    if (masterState == 0) {
        // BACK in game list returns to main boot menu
        if (evt == BTN_EVT_BACK_SHORT || evt == BTN_EVT_BACK_LONG) {
            extern void change_ui_state(ui_state_t new_state);
            change_ui_state(UI_STATE_BOOT);
            return;
        }

        if (evt == BTN_EVT_UP_SHORT || evt == BTN_EVT_UP_LONG) {
            if (registeredGamesCount > 0) {
                masterMenuOption = (masterMenuOption > 0) ? masterMenuOption - 1 : registeredGamesCount - 1;
            }
        }
        else if (evt == BTN_EVT_DOWN_SHORT || evt == BTN_EVT_DOWN_LONG) {
            if (registeredGamesCount > 0) {
                masterMenuOption = (masterMenuOption < registeredGamesCount - 1) ? masterMenuOption + 1 : 0;
            }
        }
        else if (evt == BTN_EVT_SELECT_SHORT) {
            if (registeredGamesCount > 0) {
                currentGameIndex = sortedIndices[masterMenuOption];
                gamesRegistry[currentGameIndex].init_cb();
                masterState = 1;
            }
        }

        oled_fb_clear();
        oled_fb_draw_string(14, 0, "- ARCADE GAMES -");
        
        int startItem = masterMenuOption >= 4 ? masterMenuOption - 3 : 0;
        
        for (int i = 0; i < 4; i++) {
            int idx = startItem + i;
            if (idx >= registeredGamesCount) break;
            
            if (masterMenuOption == idx) oled_fb_draw_string(10, 15 + (i * 12), ">");
            
            int realIdx = sortedIndices[idx];
            oled_fb_draw_string(22, 15 + (i * 12), gamesRegistry[realIdx].name);
        }
    }
    else if (masterState == 1) {
        if (currentGameIndex >= 0 && currentGameIndex < registeredGamesCount) {
            gamesRegistry[currentGameIndex].update_cb(evt);
        } else {
            arcade_return_to_menu();
        }
    }
}

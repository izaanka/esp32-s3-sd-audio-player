#include "arcade_lightsout.h"
#include "arcade_engine.h"
#include "oled_display.h"
#include "button_handler.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const bool inv3x3[9][9] = {
    {1, 0, 1, 0, 0, 1, 1, 1, 0}, {0, 0, 0, 0, 1, 0, 1, 1, 1}, {1, 0, 1, 1, 0, 0, 0, 1, 1},
    {0, 0, 1, 0, 1, 1, 0, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1, 0}, {1, 0, 0, 1, 1, 0, 1, 0, 0},
    {1, 1, 0, 0, 0, 1, 1, 0, 1}, {1, 1, 1, 0, 1, 0, 0, 0, 0}, {0, 1, 1, 1, 0, 0, 1, 0, 1}
};

static bool grid[6][12]; 
static int cursorIndex = 0; 
static bool showHint = false;
static int loMenuOption = 0;
static int gridW = 3, gridH = 3, totalTiles = 9;
static int spacing = 20, boxSize = 18, lightOffset = 4, lightSize = 10;
static int state = 10;
static uint32_t last_input_time = 0;
static bool select_held = false;
static int select_hold_time = 0;

static void toggleTile(int x, int y) {
    if (x >= 0 && x < gridW && y >= 0 && y < gridH) { 
        grid[y][x] = !grid[y][x]; 
    }
}

static void pressButton(int x, int y) {
    toggleTile(x, y); 
    toggleTile(x - 1, y); 
    toggleTile(x + 1, y);
    toggleTile(x, y - 1); 
    toggleTile(x, y + 1);
}

static void generatePuzzle() {
    for (int y = 0; y < gridH; y++) { 
        for (int x = 0; x < gridW; x++) { 
            grid[y][x] = false; 
        } 
    }
    int randomMoves = (esp_random() % (totalTiles * 3)) + (totalTiles * 2);
    for (int i = 0; i < randomMoves; i++) { 
        pressButton(esp_random() % gridW, esp_random() % gridH); 
    }
    cursorIndex = 0; 
    state = 11;
    showHint = false;
}

void arcade_lightsout_init(void) {
    loMenuOption = 0;
    state = 10;
    last_input_time = 0;
    select_held = false;
    select_hold_time = 0;
}

void arcade_lightsout_update(button_event_t evt) {
    if (state == 10) { 
        if (evt == BTN_EVT_UP_SHORT || evt == BTN_EVT_UP_LONG) {
            loMenuOption = (loMenuOption > 0) ? loMenuOption - 1 : 2;
        }
        else if (evt == BTN_EVT_DOWN_SHORT || evt == BTN_EVT_DOWN_LONG) {
            loMenuOption = (loMenuOption < 2) ? loMenuOption + 1 : 0;
        }
        else if (evt == BTN_EVT_SELECT_SHORT) {
            if (loMenuOption == 0) { gridW = 3; gridH = 3; spacing = 20; boxSize = 18; lightOffset = 4; lightSize = 10; }
            else if (loMenuOption == 1) { gridW = 5; gridH = 5; spacing = 12; boxSize = 10; lightOffset = 2; lightSize = 6; }
            else if (loMenuOption == 2) { gridW = 12; gridH = 6; spacing = 10; boxSize = 8; lightOffset = 2; lightSize = 4; }
            totalTiles = gridW * gridH;
            generatePuzzle();
        }
        else if (evt == BTN_EVT_BACK_SHORT || evt == BTN_EVT_BACK_LONG) {
            arcade_return_to_menu();
            return;
        }
        
        oled_fb_clear();
        oled_fb_draw_string(10, 10, "Select Grid Size:");
        const char* options[] = {"3x3 (Easy)", "5x5 (Normal)", "12x6 (Extreme)"};
        for(int i = 0; i < 3; i++) {
            if(i == loMenuOption) {
                oled_fb_draw_string(14, 30 + (i * 10), ">");
            }
            oled_fb_draw_string(24, 30 + (i * 10), options[i]);
        }

    } 
    else if (state == 11) { 
        if (evt == BTN_EVT_BACK_SHORT || evt == BTN_EVT_BACK_LONG) {
            arcade_return_to_menu();
            return;
        }
        
        if (evt == BTN_EVT_UP_SHORT || evt == BTN_EVT_UP_LONG) {
            cursorIndex = (cursorIndex > 0) ? cursorIndex - 1 : totalTiles - 1; 
        }
        else if (evt == BTN_EVT_DOWN_SHORT || evt == BTN_EVT_DOWN_LONG) {
            cursorIndex = (cursorIndex < totalTiles - 1) ? cursorIndex + 1 : 0; 
        }
        else if (evt == BTN_EVT_SELECT_SHORT) {
            int cX = cursorIndex % gridW;
            int cY = cursorIndex / gridW;
            pressButton(cX, cY); 
            showHint = false;
            
            bool allOff = true;
            for (int y = 0; y < gridH; y++) {
                for (int x = 0; x < gridW; x++) { 
                    if (grid[y][x] == true) allOff = false; 
                }
            }
            if (allOff) state = 12;
        }
    
        oled_fb_clear();
        int offsetX = (128 - (gridW * spacing)) / 2;
        int offsetY = (64 - (gridH * spacing)) / 2;
        
        for (int y = 0; y < gridH; y++) {
            for (int x = 0; x < gridW; x++) {
                int drawX = offsetX + (x * spacing);
                int drawY = offsetY + (y * spacing);
                oled_fb_draw_rect(drawX, drawY, boxSize, boxSize);
                
                if (grid[y][x]) { 
                    oled_fb_fill_rect(drawX + lightOffset, drawY + lightOffset, lightSize, lightSize); 
                }
                if (cursorIndex == (y * gridW + x)) { 
                    oled_fb_draw_rect(drawX - 2, drawY - 2, boxSize + 4, boxSize + 4); 
                }
                
                if (showHint && gridW == 3) {
                    bool needsPress = false;
                    int tileIndex = y * 3 + x;
                    for (int i = 0; i < 9; i++) {
                        if (grid[i / 3][i % 3] && inv3x3[tileIndex][i]) needsPress = !needsPress;
                    }
                    if (needsPress) {
                        oled_fb_draw_line(drawX, drawY, drawX + boxSize, drawY + boxSize);
                        oled_fb_draw_line(drawX + boxSize, drawY, drawX, drawY + boxSize);
                    }
                }
            }
        }

    } 
    else if (state == 12) { 
        if (evt == BTN_EVT_SELECT_SHORT || evt == BTN_EVT_BACK_SHORT || evt == BTN_EVT_BACK_LONG) {
            arcade_return_to_menu();
            return;
        }
        
        oled_fb_clear();
        oled_fb_draw_string(25, 20, "PUZZLE BEATEN!");
        oled_fb_draw_string(25, 40, "Press SELECT");

    }
}

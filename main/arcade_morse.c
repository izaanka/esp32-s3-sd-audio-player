#include "arcade_morse.h"
#include "arcade_engine.h"
#include "oled_display.h"
#include "button_handler.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

static uint32_t pressStartTime = 0;
static uint32_t releaseStartTime = 0;
static bool isPressed = false;
static bool processingChar = false;
static bool spaceAdded = true;

static const int dotMaxDuration = 200;   
static const int dashMaxDuration = 1000;  
static const int charGap = 1400;         
static const int wordGap = 5000; 

static char currentMorse[16];
static char displayBuffer[44]; 

static const char* letters[26] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", 
    ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", 
    "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.." 
};
static const char* numbers[10] = {
    "-----", ".----", "..---", "...--", "....-", 
    ".....", "-....", "--...", "---..", "----."  
};

static void processNewCharacter(char c) {
    int len = strlen(displayBuffer);
    if (len >= 42) {
        memmove(displayBuffer, displayBuffer + 1, 42);
        displayBuffer[41] = c;
        displayBuffer[42] = '\0';
    } else {
        displayBuffer[len] = c;
        displayBuffer[len + 1] = '\0';
    }
}

static void decodeAndPrint(const char* morseSequence) {
    char decodedChar = '?'; 
    for (int i = 0; i < 26; i++) {
        if (strcmp(letters[i], morseSequence) == 0) {
            decodedChar = 'A' + i;
            break;
        }
    }
    if (decodedChar == '?') {
        for (int i = 0; i < 10; i++) {
            if (strcmp(numbers[i], morseSequence) == 0) {
                decodedChar = '0' + i;
                break;
            }
        }
    }
    processNewCharacter(decodedChar);
}

void arcade_morse_init(void) {
    currentMorse[0] = '\0';
    displayBuffer[0] = '\0';
    isPressed = false;
    processingChar = false;
    spaceAdded = true;
    pressStartTime = 0;
    releaseStartTime = esp_timer_get_time() / 1000;
}

void arcade_morse_update(button_event_t evt) {
    if (evt == BTN_EVT_BACK_SHORT || evt == BTN_EVT_BACK_LONG) {
        arcade_return_to_menu();
        return;
    }

    // In original code btnSelect was held down for dots/dashes
    // Our button driver polls state and returns events on release.
    // However, to do morse code we need raw state holding.
    // The driver doesn't expose raw held state easily except through raw gpio read.
    // Alternatively, we use esp-idf gpio read.
    bool currentSensorState = (gpio_get_level(PIN_BTN_SELECT) == 0); // Active LOW
    uint32_t currentTime = esp_timer_get_time() / 1000;

    if (currentSensorState && !isPressed) {
        isPressed = true;
        pressStartTime = currentTime;
        spaceAdded = false;
    }
    else if (!currentSensorState && isPressed) {
        isPressed = false;
        releaseStartTime = currentTime;
        
        uint32_t pressDuration = currentTime - pressStartTime;
        
        if (pressDuration >= 200 && pressDuration <= dashMaxDuration) {
            if (strlen(currentMorse) < sizeof(currentMorse) - 1) strcat(currentMorse, "-");
            processingChar = true;
        } else if (pressDuration >= 20 && pressDuration < dotMaxDuration) {
            if (strlen(currentMorse) < sizeof(currentMorse) - 1) strcat(currentMorse, ".");
            processingChar = true;
        }
    }

    if (!isPressed && processingChar) {
        if ((currentTime - releaseStartTime) >= charGap) {
            if (strlen(currentMorse) > 0) {
                decodeAndPrint(currentMorse);
            }
            currentMorse[0] = '\0';           
            processingChar = false;      
        }
    }

    if (!isPressed && !processingChar && releaseStartTime > 0) {
        if (!spaceAdded && (currentTime - releaseStartTime) >= wordGap) {
            processNewCharacter(' ');             
            spaceAdded = true;           
        }
    }

    oled_fb_clear();
    oled_fb_draw_string(0, 0, "Morse Translator");

    if (isPressed) {
        oled_fb_fill_rect(115, 0, 10, 10);
    } else {
        oled_fb_draw_rect(115, 0, 10, 10);
    }
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Input: %s", currentMorse);
    oled_fb_draw_string(0, 15, buf);
    
    if (strlen(displayBuffer) > 21) {
        char line1[22];
        strncpy(line1, displayBuffer, 21);
        line1[21] = '\0';
        oled_fb_draw_string(0, 30, line1);
        oled_fb_draw_string(0, 40, displayBuffer + 21);
    } else {
        oled_fb_draw_string(0, 30, displayBuffer);
    }
    
    oled_fb_draw_string(0, 56, "< Exit");

}

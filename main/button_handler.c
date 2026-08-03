#include "button_handler.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "BUTTONS";

typedef struct {
    int gpio;
    button_event_t evt_short;
    button_event_t evt_long;
    bool has_long;
    bool last_state;
    bool current_state;
    uint32_t last_debounce_time;
    uint32_t press_start_time;
    bool long_fired;
} button_state_t;

static button_state_t buttons[] = {
    {PIN_BTN_SELECT, BTN_EVT_SELECT_SHORT, BTN_EVT_NONE, false, true, true, 0, 0, false},
    {PIN_BTN_UP, BTN_EVT_UP_SHORT, BTN_EVT_UP_LONG, true, true, true, 0, 0, false},
    {PIN_BTN_DOWN, BTN_EVT_DOWN_SHORT, BTN_EVT_DOWN_LONG, true, true, true, 0, 0, false},
    {PIN_BTN_BACK, BTN_EVT_BACK_SHORT, BTN_EVT_BACK_LONG, true, true, true, 0, 0, false}
};

#define NUM_BUTTONS (sizeof(buttons)/sizeof(buttons[0]))

void button_init(void) {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1;
    io_conf.pull_down_en = 0;
    
    uint64_t pin_mask = 0;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        pin_mask |= (1ULL << buttons[i].gpio);
    }
    io_conf.pin_bit_mask = pin_mask;
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "Buttons initialized");
}

button_event_t button_poll(void) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    button_event_t ret_evt = BTN_EVT_NONE;
    
    for (int i = 0; i < NUM_BUTTONS; i++) {
        button_state_t *btn = &buttons[i];
        bool reading = (gpio_get_level(btn->gpio) == 1);
        
        if (reading != btn->last_state) {
            btn->last_debounce_time = now;
        }
        
        if ((now - btn->last_debounce_time) > BTN_DEBOUNCE_MS) {
            if (reading != btn->current_state) {
                btn->current_state = reading;
                
                if (btn->current_state == false) {
                    // Pressed
                    btn->press_start_time = now;
                    btn->long_fired = false;
                } else {
                    // Released
                    if (!btn->long_fired && ret_evt == BTN_EVT_NONE) {
                        ret_evt = btn->evt_short;
                    }
                }
            }
        }
        
        // Check long press
        if (btn->current_state == false && btn->has_long && !btn->long_fired) {
            if ((now - btn->press_start_time) > BTN_LONG_PRESS_MS) {
                btn->long_fired = true;
                if (ret_evt == BTN_EVT_NONE) {
                    ret_evt = btn->evt_long;
                }
            }
        }
        
        btn->last_state = reading;
    }
    
    return ret_evt;
}

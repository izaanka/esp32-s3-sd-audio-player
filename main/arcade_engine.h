#pragma once
#include <stdbool.h>
#include "button_handler.h"

void arcade_init(void);
void arcade_update(button_event_t evt);
void arcade_return_to_menu(void);

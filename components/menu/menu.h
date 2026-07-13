#ifndef MENU_H
#define MENU_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdint.h>

typedef enum {
  MENU_INTENT_NONE = 0,
  MENU_INTENT_SYNC,
  MENU_INTENT_SLEEP,
} menu_intent_t;

void menu_init(EventGroupHandle_t button_group);
void menu_enter(void);
menu_intent_t menu_act(void);
void menu_step(void);
void menu_exit(void);

#endif

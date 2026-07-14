#ifndef MENU_H
#define MENU_H

#include "esp_err.h"

typedef enum {
  MENU_INTENT_NONE = 0,
  MENU_INTENT_SYNC,
  MENU_INTENT_SLEEP,
} menu_intent_t;

esp_err_t menu_init(void);
void menu_enter(void);
menu_intent_t menu_act(void);
void menu_step(void);
void menu_exit(void);

#endif

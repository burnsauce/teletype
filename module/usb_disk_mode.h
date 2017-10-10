#ifndef _USB_DISK_MODE_H_
#define _USB_DISK_MODE_H_
#include <stdint.h>

#include "state.h"

void tele_usb_disk(void);

typedef enum {
    UM_TOP,
    UM_SAVE,
    UM_SAVE_BANK,
    UM_SAVE_SCENE_SRC,
    UM_SAVE_SCENE_DST_BANK,
    UM_SAVE_SCENE_DST_SCENE,
    UM_SAVE_CONFIRM,
    UM_LOAD,
    UM_LOAD_BANK,
    UM_LOAD_BANK_CONFIRM,
    UM_LOAD_SCENE_BANK,
    UM_LOAD_SCENE_SRC,
    UM_LOAD_SCENE_DST,
    UM_LOAD_CONFIRM,
    UM_OK,
    UM_ERROR
} ud_menu_state_t;


typedef struct {
    uint8_t data[32];
} ud_banks_t;

typedef enum {
    UM_LIVE_SCENE = SCRIPT_COUNT
} ud_scene_t;

typedef struct {
    uint8_t data[5];
} ud_scenes_t;

#endif

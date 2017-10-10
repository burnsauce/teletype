#ifndef _FLASH_H_
#define _FLASH_H_

#include <stdint.h>

#include "globals.h"
#include "line_editor.h"
#include "teletype.h"

#define SCENE_SLOTS 32

// NVRAM data structure located in the flash array.
typedef const struct {
    scene_script_t scripts[SCRIPT_COUNT];
    scene_pattern_t patterns[PATTERN_COUNT];
    char text[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
} nvram_scene_t;

typedef const struct {
    nvram_scene_t scenes[SCENE_SLOTS];
    uint8_t last_scene;
    uint8_t fresh;
} nvram_data_t;

void flash_prepare(void);
void flash_read(uint8_t preset_no, scene_state_t *scene,
                char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]);
void flash_write(uint8_t preset_no, scene_state_t *scene,
                 char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]);
uint8_t flash_last_saved_scene(void);
void flash_update_last_saved_scene(uint8_t preset_no);
const char *flash_scene_text(uint8_t preset_no, size_t line);
void flash_update_cal(cal_data_t *);
void flash_get_cal(cal_data_t *);

#endif

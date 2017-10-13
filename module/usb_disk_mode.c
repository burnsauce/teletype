#include "usb_disk_mode.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

// this
#include "flash.h"
#include "globals.h"
#include "teletype.h"

// libavr32
#include "font.h"
#include "region.h"
#include "util.h"

// asf
#include "delay.h"
#include "fat.h"
#include "file.h"
#include "fs_com.h"
#include "navigation.h"
#include "print_funcs.h"
#include "uhi_msc.h"
#include "uhi_msc_mem.h"
#include "usb_protocol_msc.h"
#include "flashc.h"

// Helper Macros

#ifdef GREATEST_H // Inside test framework
#define INLINE
#define PRIVATE
#else
#define INLINE inline
#define PRIVATE static
#endif

PRIVATE bool     ud_bank_scene_exists(uint8_t);
PRIVATE bool     ud_deserialize_scene(clone_scene_t*);
PRIVATE void     ud_exit(void);
PRIVATE bool     ud_flash_bank_is_empty(void);
PRIVATE uint8_t  ud_flash_scene_count(void);
PRIVATE bool     ud_flash_scene_is_empty(ud_scene_t);
PRIVATE uint8_t  ud_get_bank_count(void);
PRIVATE uint8_t  ud_get_bank_scene_count(void);
PRIVATE void     ud_get_bank_scenes(void);
PRIVATE void     ud_get_bank_scenes(void);
PRIVATE void     ud_get_banks(void);
PRIVATE uint8_t  ud_get_flash_scene_count(void);
PRIVATE void     ud_get_flash_scenes(void);
PRIVATE void     ud_get_flash_scenes(void);
PRIVATE void     ud_init_menu(void);
PRIVATE bool     ud_init(void);
PRIVATE uint8_t  ud_knob_scale(uint8_t);
PRIVATE bool     ud_live_scene_is_empty(void);
PRIVATE bool     ud_load_bank(uint8_t);
PRIVATE bool     ud_load_scene_from_disk(uint8_t,clone_scene_t*);
PRIVATE bool     ud_load_scene(void);
PRIVATE void     ud_make_path_bank(bool);
PRIVATE void     ud_make_path(uint8_t);
PRIVATE bool     ud_mount_drive(void);
PRIVATE uint8_t  ud_new_bank_scene(void);
PRIVATE bool     ud_new_bank(void);
PRIVATE uint8_t  ud_new_flash_scene(void);
PRIVATE uint8_t  ud_next_bank_number(void);
PRIVATE void     ud_popup_timeout(void);
PRIVATE bool     ud_save_bank(void);
PRIVATE bool     ud_save_scene(void);
PRIVATE void     ud_select_bank(uint8_t);
PRIVATE bool     ud_serialize_scene(nvram_scene_t*);

// Draw routine prototypes
PRIVATE void ud_draw_bank_scene_selector(bool);
PRIVATE void ud_draw_bank_selector(bool);
PRIVATE void ud_draw_error_popup(void);
PRIVATE void ud_draw_flash_scene_selector(bool);
PRIVATE void ud_draw_load_bank_confirm_dialog(void);
PRIVATE void ud_draw_load_confirm_dialog(void);
PRIVATE void ud_draw_load_menu(void);
PRIVATE void ud_draw_ok_popup(void);
PRIVATE void ud_draw_save_confirm_dialog(void);
PRIVATE void ud_draw_save_menu(void);
PRIVATE void ud_draw_top_menu(void);

// File Variables
PRIVATE struct ud_menu {
    bool             invalid;
    ud_menu_state_t  state;
    ud_menu_state_t  last_state;
    ud_scenes_t      flash_scenes;
    uint32_t         bank_scenes;
    ud_banks_t       banks;
    uint8_t          bank;
    ud_scene_t       src;
    ud_scene_t       dst;
    clone_scene_t    blank;
} menu;

PRIVATE nvram_data_t *fp;
PRIVATE uint32_t knob_last = 0;
PRIVATE bool dirty = false;

// Public interface

bool screen_refresh_usbdisk(void) {
    if (!dirty)
        return false;

    switch (menu.state) {
        case UM_TOP:
            ud_draw_top_menu();
            break;

        case UM_SAVE:
            ud_draw_save_menu();
            break;

        case UM_SAVE_BANK:
            ud_draw_bank_selector(true);
            break;

        case UM_SAVE_SCENE_SRC:
            ud_draw_flash_scene_selector(false);
            break;

        case UM_SAVE_SCENE_DST_BANK:
            ud_draw_bank_selector(true);
            break;

        case UM_SAVE_SCENE_DST_SCENE:
            ud_draw_bank_scene_selector(true);
            break;

        case UM_SAVE_CONFIRM:
            ud_draw_save_confirm_dialog();
            break;

        case UM_LOAD:
            ud_draw_load_menu();
            break;

        case UM_LOAD_BANK:
            ud_draw_bank_selector(false);
            break;

        case UM_LOAD_BANK_CONFIRM:
            ud_draw_load_bank_confirm_dialog();
            break;

        case UM_LOAD_SCENE_BANK:
            ud_draw_bank_selector(false);
            break;

        case UM_LOAD_SCENE_SRC:
            ud_draw_bank_scene_selector(false);
            break;

        case UM_LOAD_SCENE_DST:
            ud_draw_flash_scene_selector(true);
            break;

        case UM_LOAD_CONFIRM:
            ud_draw_load_confirm_dialog();
            break;

        case UM_OK:
            ud_draw_ok_popup();
            break;

        case UM_ERROR:
            ud_draw_error_popup();
            break;
    }
    dirty = false;
    return true;
}

void set_usbdisk_mode(void) {
    if ( !ud_init() ) {
        menu.state = UM_ERROR;
        menu.invalid = true;
    }
}

void process_usbdisk_knob(int32_t knob_value) {
    if (knob_value != knob_last) {
        knob_last = knob_value;
        dirty = true;
    }
}

void process_usbdisk_button_press(bool held) {
    if (held == true)
        return;
    
    uint16_t selection;
    uint8_t count;
    
    if (menu.state == UM_OK || menu.state == UM_ERROR) {
        menu.state = menu.last_state;
        return;
    }

    if (menu.state != UM_SAVE_CONFIRM && menu.state != UM_LOAD_CONFIRM)
        menu.last_state = menu.state;

    switch (menu.state) {
        case UM_TOP:
            /* Save
             * Load
             * Exit */
            selection = ud_knob_scale(3);
            if (selection == 0)
                menu.state = UM_SAVE;
            else if (selection == 1)
                menu.state = UM_LOAD;
            else
                ud_exit();
            break;

        case UM_SAVE:
            /* Bank
             * Scene
             * Back */
            selection = ud_knob_scale(3);
            if (selection == 0)
                menu.state = UM_SAVE_BANK;
            else if (selection == 1)
                menu.state = UM_SAVE_SCENE_SRC;
            else
                menu.state = UM_TOP;
            break;

        case UM_SAVE_BANK:
            /* Selector: 1 to Bank Count
             * New
             * Back */
            count = ud_get_bank_count();
            selection = ud_knob_scale(count + 2);
            if (selection == count + 1)
                menu.state = UM_SAVE;
            else if (selection == count) {
                ud_new_bank();
                if(ud_save_bank())
                    menu.state = UM_OK;
                else
                    menu.state = UM_ERROR;
            }
            else
                menu.state = UM_SAVE_CONFIRM;
            break;

        case UM_SAVE_SCENE_SRC:
            /* Selector: 1 to Scene Count incl LIVE 
             * Back */
            count = ud_get_flash_scene_count();
            selection = ud_knob_scale(count + 1);
            if (selection == count)
                menu.state = UM_SAVE;
            else {
                menu.src = selection;
                menu.state = UM_SAVE_SCENE_DST_BANK;
            }
            break;

        case UM_SAVE_SCENE_DST_BANK:
            /* Selector: 1 to bank count
             * New
             * Back */
            count = ud_get_bank_count();
            selection = ud_knob_scale(count + 2);
            if (selection == count + 1)
                menu.state = UM_SAVE_SCENE_SRC;
            else {
                if (selection == count)
                    ud_new_bank();
                else
                    ud_select_bank(selection);
                menu.state = UM_SAVE_SCENE_DST_SCENE;
            }   
                
        case UM_SAVE_SCENE_DST_SCENE:
            /* Selector: 1 to Scene Count
             * New
             * Back */
            count = ud_get_bank_scene_count();
            selection = ud_knob_scale(count + 2);
            if (selection == count + 1)
                menu.state = UM_SAVE_SCENE_DST_BANK;
            else if (selection == count) {
                menu.dst = ud_new_bank_scene();
                if (ud_save_scene())
                    menu.state = UM_OK;
                else
                    menu.state = UM_ERROR;
            }
            else {
                menu.dst = selection;
                menu.state = UM_SAVE_CONFIRM;
            }
            break;

        case UM_SAVE_CONFIRM:
            /* Yes
             * No */
            selection = ud_knob_scale(2);
            if (selection == 0) {
                if (ud_save_scene())
                    menu.state = UM_OK;
                else
                    menu.state = UM_ERROR;
            }
            else
                menu.state = menu.last_state;
            break;

        case UM_LOAD:
            /* Bank
             * Scene
             * Back */
            selection = ud_knob_scale(3);
            if (selection == 0)
                menu.state = UM_LOAD_BANK;
            else if (selection == 1)
                menu.state = UM_LOAD_SCENE_BANK;
            else
                menu.state = UM_TOP;
            break;

        case UM_LOAD_BANK:
            /* Selector: 1 to last bank
             * Back */
            count = ud_get_bank_count();
            selection = ud_knob_scale(count + 1);
            if (selection == count)
                menu.state = UM_LOAD;
            else {
                if (ud_flash_bank_is_empty()) {
                    if (ud_load_bank(selection))
                        menu.state = UM_OK;
                    else
                        menu.state = UM_ERROR;
                }
                else
                    menu.state = UM_LOAD_BANK_CONFIRM;
            }
            break;

        case UM_LOAD_BANK_CONFIRM:
            /* Yes
             * No */
            selection = ud_knob_scale(2);
            if (selection == 0) {
                if (ud_load_bank(selection))
                    menu.state = UM_OK;
                else
                    menu.state = UM_ERROR;
            }
            else
                menu.state = UM_LOAD_BANK;
            break;

        case UM_LOAD_SCENE_BANK:
            /* Selector: 1 to last bank
             * Back */
            count = ud_get_bank_count();
            selection = ud_knob_scale(count + 1);
            if (selection == count)
                menu.state = UM_LOAD_BANK;
            else {
                ud_load_bank(selection);
                menu.state = UM_LOAD_SCENE_SRC;
            }
            break;

        case UM_LOAD_SCENE_SRC:
            /* Selector: 1 to last bank
             * Back */
            count = ud_get_bank_scene_count();
            selection = ud_knob_scale(count + 1);
            if (selection == count)
                menu.state = UM_LOAD_SCENE_BANK;
            else {
               menu.src = selection;
               menu.state = UM_LOAD_SCENE_DST;
            }
            break;

        case UM_LOAD_SCENE_DST:
            /* Selector: 1 to last bank
             * New
             * Back */
            count = ud_get_flash_scene_count();
            selection = ud_knob_scale(count + 2);
            if (selection == count + 1)
                menu.state = UM_LOAD_SCENE_SRC;
            else if (selection == count) {
                menu.dst = ud_new_flash_scene();
                if(ud_load_scene())
                    menu.state = UM_OK;
                else
                    menu.state = UM_ERROR;
            }
            else {
                menu.dst = selection;
                menu.state = UM_LOAD_CONFIRM;
            }
            break;

        case UM_LOAD_CONFIRM:
            /* Yes
             * No */
            selection = ud_knob_scale(2);
            if (ud_load_scene())
                menu.state = UM_OK;
            else
                menu.state = UM_ERROR;
            break;
        
        case UM_OK:
        case UM_ERROR:
            menu.state = menu.last_state;
            break;
    }
    dirty = true;
}

void ud_popup_timeout(void) {
    if (menu.invalid)
        ud_exit();
    menu.state = menu.last_state;
    dirty = true;
}

// Function Bodies

PRIVATE INLINE bool ud_bank_scene_exists(uint8_t slot) {
    return ud_load_scene_from_disk(slot, NULL);
}

PRIVATE INLINE bool ud_flash_scene_is_empty(uint8_t slot) {
    return memcmp(&menu.blank, &fp->scenes[slot],
                  sizeof(nvram_scene_t)) == 0;
}


PRIVATE INLINE uint8_t ud_knob_scale(uint8_t s) {
    uint32_t t = knob_last << 1;
    t *= s;
    t /= 16384;
    t += 1;
    t = t >> 1;
    return (uint8_t)t;
}

// Flash access

PRIVATE bool ud_live_scene_is_empty(void) {
    clone_scene_t s;
    memcpy(&s.scripts, ss_scripts_ptr(&scene_state),
            ss_scripts_size());
    memcpy(&s.patterns, ss_patterns_ptr(&scene_state),
            ss_patterns_size());
    // Text not stored in live scene
    return memcmp(&menu.blank, &s, sizeof(clone_scene_t)) == 0;
}

PRIVATE bool ud_flash_bank_is_empty(void) {
    for(int i = 0; i < SCENE_SLOTS; i++)
        if (!ud_flash_scene_is_empty(i))
            return false;
    return true;
}

PRIVATE uint8_t ud_get_flash_scene_count(void) {
    uint8_t count = 0;
    for(int i = 0; i < SCENE_SLOTS; i++)
        if (!ud_flash_scene_is_empty(i))
            count++;
    return count;
}

PRIVATE uint8_t ud_new_flash_scene(void) {
    for(int i = 0; i < SCENE_SLOTS; i++)
        if (ud_flash_scene_is_empty(i))
            return i;
    // Error
    return 31;
}

PRIVATE void ud_get_flash_scenes(void) {
    uint8_t slot = 0;
    for (int i = 0; i < SCENE_SLOTS; i++) {
        if (slot % 8 == 0 && i != 0)
            slot++;
        if (!ud_flash_scene_is_empty(i))
            menu.flash_scenes.data[slot] |= (1 << (i % 8));
    }
    if (!ud_live_scene_is_empty())
        menu.flash_scenes.data[4] = 1;
}

// USB Bank access
PRIVATE char ud_path[] = "/teletype/bank_000/scene_00.txt";
//                       0123456789012345678901234567890

PRIVATE void ud_make_path_bank(bool terminate) {
    if (menu.bank > 100)
        itoa(menu.bank / 100, &ud_path[15], 1);
    else
        ud_path[15] = '0';
    if (menu.bank % 100 > 10)
        itoa((menu.bank % 100) / 10, &ud_path[16], 1);
    else
        ud_path[16] = '0';
    itoa(menu.bank % 10, &ud_path[17], 1);
    if (terminate)
        ud_path[19] = '\0';
    else
        ud_path[19] = 's';
}

PRIVATE void ud_make_path(uint8_t scene) {
    ud_make_path_bank(false);

    if (scene >= 10)
        itoa(scene / 10, &ud_path[25], 1);
    else
        ud_path[25] = '0';
    itoa(scene % 10, &ud_path[26], 1);
}

PRIVATE bool ud_load_scene_from_disk(uint8_t slot, clone_scene_t *s) {
    ud_make_path(slot);

    if (!nav_setcwd((FS_STRING)ud_path, true, false))
        return false;

    if (s == NULL)
        return true;

    if (!file_open(FOPEN_MODE_R))
        return false;

    bool ret = ud_deserialize_scene(s);
    file_close();

    return ret;
}

PRIVATE bool ud_save_scene(void) {
    ud_make_path(menu.dst);

    nav_setcwd((FS_STRING)ud_path, true, true);
    
    if (!file_open(FOPEN_MODE_W))
        return false;

    bool ret = ud_serialize_scene(&fp->scenes[menu.src]);
    file_close();

    return ret;
}

PRIVATE bool ud_save_bank(void) {
    bool ret = true;
    for (uint8_t i = 0; i < 32; i++) {
        menu.src = i;
        menu.dst = i;
        if (!ud_save_scene())
            ret = false;
    }
    return ret;
}

PRIVATE void ud_get_banks(void) {
    char buf[14];
    nav_setcwd((FS_STRING)"/teletype/", true, true);
    nav_filelist_single_enable(FS_DIR);
    nav_filelist_set(0, FS_FIND_NEXT);
    while (!nav_filelist_eol()) {
        nav_dir_name((FS_STRING)buf, 13);
        if (strncmp("bank_", buf, 5) && strlen(buf) == 8) {
            uint8_t bank = atoi(&buf[5]);
            menu.banks.data[bank / 8] |= (1 << (bank % 8));
        }
        nav_filelist_set(1, FS_FIND_NEXT);
    }
    nav_filelist_single_disable();
}

PRIVATE void ud_get_bank_scenes(void) {
    ud_make_path_bank(true);
    nav_setcwd((FS_STRING)ud_path, true, true);
    nav_filelist_single_enable(FS_FILE);
    

    char buf[14];
    while (!nav_filelist_eol()) {
        nav_file_getname((FS_STRING)buf, 13);
        if (strncmp("scene_", buf, 6) == 0 && strncmp(".txt", &buf[9], 4) == 0) {
            uint8_t scene = atoi(&buf[6]);
            menu.bank_scenes |= (1 << scene);
        }
        nav_filelist_set(1, FS_FIND_NEXT);
    }
    nav_filelist_single_disable();
}

PRIVATE uint8_t ud_get_bank_scene_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < 32; i++) {
        if ((menu.bank_scenes >> i) & 1)
            count++;
    }
    return count;
}

PRIVATE bool ud_banks_full = false;

PRIVATE uint8_t ud_next_bank_number(void) {
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 32; j++)
            if (!((menu.banks.data[i] >> j) & 1))
                return 32 * i + j;
    ud_banks_full = true;
    return UINT8_MAX; // technically an error
}

PRIVATE uint8_t ud_get_bank_count(void) {
    uint8_t count = 0;

    for (int i = 0; i < 8; i++)
        for (int j=0; j < 32; j++)
            if ((menu.banks.data[i] >> j) & 1)
                count++;
    return count;
}

PRIVATE bool ud_new_bank(void) {
    uint8_t bank = ud_next_bank_number();
    if (ud_banks_full)
        return false;
    ud_select_bank(bank);
    return true;
}

PRIVATE void ud_select_bank(uint8_t bank) {
    menu.bank = bank;
    ud_make_path_bank(true);
    nav_setcwd((FS_STRING)ud_path, true, true);
    ud_get_bank_scenes();
}

PRIVATE bool ud_load_scene(void) {
    clone_scene_t s;
    if(!ud_load_scene_from_disk(menu.src, &s))
        return false;
    flashc_memcpy((void *)&fp->scenes[menu.dst], &s, sizeof(clone_scene_t), true);
    return true;
}

PRIVATE bool ud_load_bank(uint8_t bank) {
    ud_select_bank(bank);
    for (uint8_t i = 0; i < 32; i++) {
        menu.src = i;
        menu.dst = i;
        if (!ud_load_scene())
            flashc_memcpy((void *)&fp->scenes[menu.dst], &menu.blank, sizeof(nvram_scene_t), true);
    }
    return true;
}

PRIVATE uint8_t ud_new_bank_scene() {
    for (int i = 0; i < 32; i++)
        if (((menu.bank_scenes >> i) & 1) == 0)
            return i;
    return 31;
}

PRIVATE bool ud_mount_drive(void) {
    if (!uhi_msc_is_available())
        return false;
    for (int i = 0; i < uhi_msc_get_lun(); i++)
        if (nav_drive_set(i))
            if (nav_partition_mount())
                return true;
    return false;
}

// Control flow

PRIVATE void ud_init_menu(void) {
    menu.state = UM_TOP;
    menu.bank = 0;
    memset((void *)&menu.blank, 0, sizeof(clone_scene_t));
}

PRIVATE void ud_exit(void) {
    nav_exit();
    exit_usb_mode();
    set_last_mode();
}


PRIVATE bool ud_init(void) {
    ud_init_menu();

    nav_reset();
    nav_select(0);

    ud_get_flash_scenes();
    if (ud_mount_drive()) {
        ud_get_banks();
        return true;
    }
    
    return false;
}

PRIVATE bool ud_serialize_scene(nvram_scene_t *scene) {
    if (!file_open(FOPEN_MODE_W)) {
        print_dbg("\r\nfail");
        return false;
    }

    char blank = 0;
    for (int l = 0; l < SCENE_TEXT_LINES; l++) {
        if (strlen(scene->text[l])) {
            file_write_buf((uint8_t*)scene->text[l], strlen(scene->text[l]));
            file_putc('\n');
            blank = 0;
        }
        else if (!blank) {
            file_putc('\n');
            blank = 1;
        }
    }

    char input[36];
    for (int s = 0; s < 10; s++) {
        file_putc('\n');
        file_putc('\n');
        file_putc('#');
        if (s == 8)
            file_putc('M');
        else if (s == 9)
            file_putc('I');
        else
            file_putc(s + '0');

        int line = 0;
        for (int l = 0; l < scene->scripts[s].l; l++) {
            file_putc('\n');
            print_command(&scene->scripts[s].c[l], input);
            file_write_buf((uint8_t*)input, strlen(input));
            line++;
            if (line >= SCRIPT_MAX_COMMANDS)
                break;
        }
    }

    file_putc('\n');
    file_putc('\n');
    file_putc('#');
    file_putc('P');
    file_putc('\n');

    for (int b = 0; b < 4; b++) {
        itoa(scene->patterns[b].len, input, 10);
        file_write_buf((uint8_t*)input, strlen(input));
        if (b == 3)
            file_putc('\n');
        else
            file_putc('\t');
    }

    for (int b = 0; b < 4; b++) {
        itoa(scene->patterns[b].wrap, input, 10);
        file_write_buf((uint8_t*)input, strlen(input));
        if (b == 3)
            file_putc('\n');
        else
            file_putc('\t');
    }

    for (int b = 0; b < 4; b++) {
        itoa(scene->patterns[b].start, input, 10);
        file_write_buf((uint8_t*)input, strlen(input));
        if (b == 3)
            file_putc('\n');
        else
            file_putc('\t');
    }

    for (int b = 0; b < 4; b++) {
        itoa(scene->patterns[b].end, input, 10);
        file_write_buf((uint8_t*)input, strlen(input));
        if (b == 3)
            file_putc('\n');
        else
            file_putc('\t');
    }

    file_putc('\n');

    for (int l = 0; l < 64; l++) {
        for (int b = 0; b < 4; b++) {
            itoa(scene->patterns[b].val[l], input, 10);
            file_write_buf((uint8_t*)input, strlen(input));
            if (b == 3)
                file_putc('\n');
            else
                file_putc('\t');
        }
    }

    file_close();
    return true;
}

#define BUFSIZE 8192 // 8k should hold any script in text, hopefully

typedef enum {
    _EXPECT_TEXT,
    _EXPECT_SECTION,
    _EXPECT_COMMAND,
    _EXPECT_PATTERN_HEADER,
    _EXPECT_PATTERN_DATA
} _temp_parser_t;

#define _CONT (end < bp)

PRIVATE uint8_t _find_eol(char *, char **);
PRIVATE uint8_t _find_eol(char *p, char **eol) {
    char *cr = strstr("\r", p);
    char *lf = strstr("\n", p);

#if 0
    // This is ugly as hell and there's probably a better way to do it.
    if (cr == NULL && lf != NULL)
        *eol = lf - 1;
    else if (lf == NULL && cr != NULL)
        *eol = cr - 1;
    else if (lf != NULL && cr != NULL) {
        if (cr > lf)
            *eol = lf - 1;
        else
            *eol = cr - 1;
    }
    else
        *eol = NULL;

    return (cr != NULL) + (lf != NULL);
#else
    if (cr == NULL && lf == NULL) {
        *eol = NULL;
        return 0;
    }
    if (cr != NULL && lf > cr) {
        *eol = cr - 1;
        return 2;
    }
    if (lf != NULL && cr > lf) {
        *eol = lf - 1;
        return 2;
    }
    if (cr != NULL) {
        *eol = cr - 1;
        return 1;
    }
    *eol = lf - 1;
    return 1;
#endif
}

PRIVATE bool ud_deserialize_scene(clone_scene_t *scene) {
    if (!file_open(FOPEN_MODE_R)) {
        print_dbg("\r\ncan't open");
        return false;
    }

    tele_command_t cmd;
    error_t status;
    char error_msg[TELE_ERROR_MSG_LENGTH];
    char buf[BUFSIZE];
    char *endl, *temp;
    uint16_t len;
    uint8_t line = 0, crlf, script;

    uint16_t bytesread = file_read_buf((uint8_t *)buf, BUFSIZE);
    file_close();

    _temp_parser_t state = _EXPECT_TEXT;

    for (char *bp = buf, *end = buf + bytesread; _CONT; ) {
        switch (state) {
            case _EXPECT_TEXT:
                while(*bp != '#' && _CONT && state != _EXPECT_SECTION) {
                    
                    crlf = _find_eol(bp, &endl);
                    
                    if (endl == NULL) // running out of text with no section is a
                        return false; // hard fail

                    len = endl - bp + 1;

                    if (len > 0) {
                        if (len > SCENE_TEXT_CHARS) {
                            len = SCENE_TEXT_CHARS;
                            memcpy(&scene->text[line], bp, len);
                            bp += len;
                        }
                        else {
                            memcpy(&scene->text[line], bp, len);
                            bp = endl + crlf;
                        }
                    }

                    if (++line >= SCENE_TEXT_LINES)
                        state = _EXPECT_SECTION;
                }
                if (!_CONT)
                    return false; // hard fail

                // no break to cause fall through, as we found a # or ran out of text space
                
            case _EXPECT_SECTION:
                while(*bp != '#' && _CONT)
                    bp++;
                if (!_CONT)
                    continue; // not a failure

                script = toupper(*(bp++));
                
                if (!(isdigit(script) || script == 'I' || script == 'M'))
                    break; // keep looking for a section

                crlf = _find_eol(bp, &endl);
                bp = endl + crlf + 1;
                line = 0;
                if (script == 'P')
                    state = _EXPECT_PATTERN_HEADER;
                else
                    state = _EXPECT_COMMAND;
                break;

            case _EXPECT_COMMAND:
                crlf = _find_eol(bp, &endl);
                if (endl == NULL)
                    continue;

                // if the section header shows up before a newline, stop reading commands
                temp = strstr(bp, "#");
                if (temp != NULL && endl > temp) {
                    bp = temp;
                    state = _EXPECT_SECTION;
                    break;
                }

                *(endl + 1) = 0;
                status = parse(bp, &cmd, error_msg);

                if (status == E_OK) {
                    status = validate(&cmd, error_msg);

                    if (status == E_OK) {
                        memcpy(&scene->scripts[line].c, &cmd, sizeof(cmd));
                        line++;
                        if (line >= SCRIPT_MAX_COMMANDS)
                            state = _EXPECT_SECTION;

                    }
                    else {
                        print_dbg("\r\nvalidate: ");
                        print_dbg(tele_error(status));
                        print_dbg(" >> ");
                        print_dbg("\r\nINPUT: ");
                        print_dbg(bp);
                    }
                }
                else {
                    print_dbg("\r\nERROR: ");
                    print_dbg(tele_error(status));
                    print_dbg(" >> ");
                    print_dbg("\r\nINPUT: ");
                    print_dbg(bp);
                }
                
                bp = endl + crlf + 1;
                break;

            case _EXPECT_PATTERN_HEADER:

                crlf = _find_eol(bp, &endl);
                if (endl == NULL) {
                    state = _EXPECT_SECTION;
                    break;
                }

                if (bp == endl) {
                    bp += crlf;
                    break;
                }

                *(endl + 1) = 0;
                if (sscanf(bp, "%" SCNu16 "\t%" SCNu16 "\t%" SCNu16 "\t%" SCNu16,
                            &scene->patterns[0].len,
                            &scene->patterns[1].len,
                            &scene->patterns[2].len,
                            &scene->patterns[3].len) == 4) {

                    bp = endl + crlf + 1;
                    crlf = _find_eol(bp, &endl);
                    if (endl == NULL) {
                        state = _EXPECT_SECTION;
                        break;
                    }

                    *(endl + 1) = 0;
                    if (sscanf(bp, "%" SCNu16 "\t%" SCNu16 "\t%" SCNu16 "\t%" SCNu16,
                                &scene->patterns[0].wrap,
                                &scene->patterns[1].wrap,
                                &scene->patterns[2].wrap,
                                &scene->patterns[3].wrap) == 4) {

                        bp = endl + crlf + 1;
                        crlf = _find_eol(bp, &endl);
                        if (endl == NULL) {
                            state = _EXPECT_SECTION;
                            break;
                        }

                        *(endl + 1) = 0;
                        if (sscanf(bp, "%" SCNu16 "\t%" SCNu16 "\t%" SCNu16 "\t%" SCNu16,
                                    &scene->patterns[0].start,
                                    &scene->patterns[1].start,
                                    &scene->patterns[2].start,
                                    &scene->patterns[3].start) == 4) {

                            bp = endl + crlf + 1;
                            crlf = _find_eol(bp, &endl);
                            if (endl == NULL) {
                                state = _EXPECT_SECTION;
                                break;
                            }

                            *(endl + 1) = 0;
                            if (sscanf(bp, "%" SCNu16 "\t%" SCNu16 "\t%" SCNu16 "\t%" SCNu16,
                                        &scene->patterns[0].end,
                                        &scene->patterns[1].end,
                                        &scene->patterns[2].end,
                                        &scene->patterns[3].end) == 4) {
                            }
                        }
                    }
                }

                state = _EXPECT_PATTERN_DATA;
                line = 0;

                bp = endl + crlf + 1;
                break;

            case _EXPECT_PATTERN_DATA:
                crlf = _find_eol(bp, &endl);
                if (endl == NULL) {
                    state = _EXPECT_SECTION;
                    break;
                }

                if (bp == endl) {
                    bp += crlf;
                    break;
                }

                *(endl + 1) = 0;
                if (sscanf(bp, "%" SCNu16 "\t%" SCNu16 "\t%" SCNu16 "\t%" SCNu16,
                            &scene->patterns[0].val[line],
                            &scene->patterns[1].val[line],
                            &scene->patterns[2].val[line],
                            &scene->patterns[3].val[line]) == 4)
                    line++;

                if (line == 64) {
                    state = _EXPECT_SECTION;
                    line = 0;
                }

                bp = endl + crlf + 1;
                break;
        }
    }

    return true;
}

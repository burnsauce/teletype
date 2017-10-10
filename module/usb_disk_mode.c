#include "usb_disk_mode.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

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

// Prototypes with no function written
bool     ud_bank_scene_exists(uint8_t);
void     ud_exit(void);
bool     ud_flash_scene_is_empty(ud_scene_t);
uint8_t  ud_get_bank_count(void);
uint8_t  ud_get_bank_scene_count(void);
void     ud_get_bank_scenes(void);
void     ud_get_banks(void);
uint8_t  ud_get_flash_scene_count(void);
void     ud_get_flash_scenes(void);
uint8_t  ud_knob_scale(uint8_t);
bool     ud_live_bank_is_empty(void);
bool     ud_live_scene_is_empty(void);
bool     ud_load_bank(uint8_t);
bool     ud_load_scene(uint8_t);
bool     ud_load_scene_from_disk(uint8_t, nvram_scene_t*);
bool     ud_mount_drive(void);
uint8_t  ud_new_bank_scene(void);
void     ud_new_bank(void);
uint8_t  ud_new_flash_scene(void);
bool     ud_save_bank(void);
bool     ud_save_scene(void);
bool     ud_select_bank(uint8_t);
bool     ud_deserialize_scene(nvram_scene_t*);
bool     ud_serialize_scene(void);

// Draw routine prototypes
void ud_draw_flash_scene_selector(bool);
void ud_draw_bank_scene_selector(bool);
void ud_draw_bank_selector(bool);
void ud_draw_save_confirm_dialog(void);
void ud_draw_load_bank_confirm_dialog(void);
void ud_draw_load_confirm_dialog(void);
void ud_draw_ok_popup(void);
void ud_draw_error_popup(void);
void ud_display_error(void);
void ud_display_menu(void);

// Interface
void ud_init_menu(void);
void ud_get_flash_scenes(void);
void ud_get_bank_scenes(void);
void ud_process_button_press(void);
void ud_popup_timeout(void);
bool ud_init(void);
void ud_make_path(uint8_t);

// State

struct ud_menu {
    ud_menu_state_t  state;
    ud_menu_state_t  last_state;
    ud_scenes_t      flash_scenes;
    ud_scenes_t      bank_scenes;
    ud_banks_t       banks;
    uint8_t          bank;
    ud_scene_t       src;
    ud_scene_t       dst;
    nvram_scene_t    blank;
} menu;

static nvram_data_t *fp;

// Helper Macros
#define INLINE inline

// Function Bodies

#ifdef UM_USE_MACROS

#define ud_flash_scene_is_empty(slot) \
    memcmp(&menu.blank, &fp->scenes[slot], sizeof(nvram_scene_t)) == 0

#define ud_bank_scene_exists(slot) \
    ud_load_scene_from_disk(slot, NULL)

#else

INLINE bool ud_flash_scene_is_empty(uint8_t slot) {
    return memcmp(&menu.blank, &fp->scenes[slot],
                  sizeof(nvram_scene_t)) == 0;
}

INLINE bool ud_bank_scene_exists(uint8_t slot) {
    return ud_load_scene_from_disk(slot, NULL);
}

#endif

bool ud_live_scene_is_empty(void) {
    nvram_scene_t s;
    memcpy(&s.scripts, ss_scripts_ptr(&scene_state),
            ss_scripts_size());
    memcpy(&s.patterns, ss_patterns_ptr(&scene_state),
            ss_patterns_size());
    // Text not stored in live scene
    return memcmp(&menu.blank, &s, sizeof(nvram_scene_t)) == 0;
}

static char ud_path[] = "/teletype/bank_000/scene_00.txt";
//                       0123456789012345678901234567890

void ud_make_path(uint8_t slot) {
    if (menu.bank > 100)
        itoa(menu.bank / 100, &ud_path[15], 1);
    else
        ud_path[15] = '0';
    if (menu.bank % 100 > 10)
        itoa((menu.bank % 100) / 10, &ud_path[16], 1);
    else
        ud_path[16] = '0';
    itoa(menu.bank % 10, &ud_path[17], 1);

    if (slot >= 10)
        itoa(slot / 10, &ud_path[25], 1);
    else
        ud_path[25] = '0';
    itoa(slot % 10, &ud_path[26], 1);
}

bool ud_load_scene_from_disk(uint8_t slot, nvram_scene_t *s) {
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

bool ud_save_scene(void) {
    ud_make_path(menu.dst);

    nav_setcwd((FS_STRING)ud_path, true, true);
    
    if (!file_open(FOPEN_MODE_W))
        return false;

    bool ret = ud_serialize_scene();
    file_close();

    return ret;
}

void ud_get_banks(void) {
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
}

void ud_init_menu(void) {
    menu.state = UM_TOP;
    menu.bank = 0;
    ud_get_flash_scenes();
    ud_get_banks();
    memset((void *)&menu.blank, 0, sizeof(nvram_scene_t));
}

void ud_exit(void) {
    nav_exit();
}

void ud_get_flash_scenes(void) {
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

void ud_get_bank_scenes(void) {
    uint32_t sd = 0;
    for (int i = 0; i < SCENE_SLOTS; i++)
        if (ud_bank_scene_exists(i))
            sd = sd | (1 << i);
    memcpy(&menu.bank_scenes, &sd, sizeof(sd));
}

void ud_process_button_press(void) {
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
                if (ud_live_bank_is_empty()) {
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
                selection = ud_new_flash_scene();
                if(ud_load_scene(selection))
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
            if (ud_load_scene(selection))
                menu.state = UM_OK;
            else
                menu.state = UM_ERROR;
            break;
        
        case UM_OK:
        case UM_ERROR:
            menu.state = menu.last_state;
            break;
    }
}

void ud_popup_timeout(void) {
    menu.state = menu.last_state;
}

void ud_display_menu(void) {
    switch (menu.state) {
        case UM_TOP:
            // Display the top menu
            break;

        case UM_SAVE:
            // Display the Save top menu
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
            // Display the Load top menu
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
}

void tele_usb_disk(void) {
    if ( !ud_init() )
        ud_display_error();
    else
        ud_display_menu();
}

bool ud_init(void) {
    ud_init_menu();

    nav_reset();
    nav_select(0);

    if (ud_mount_drive()) {
        ud_get_banks();
        return true;
    }
    
    return false;
}

void old_tele_usb_disk(void) {
    char input_buffer[32];
    print_dbg("\r\nusb");

    uint8_t lun_state = 0;

    for (uint8_t lun = 0; (lun < uhi_msc_mem_get_lun()) && (lun < 8); lun++) {
        // print_dbg("\r\nlun: ");
        // print_dbg_ulong(lun);

        // Mount drive
        nav_drive_set(lun);
        if (!nav_partition_mount()) {
            if (fs_g_status == FS_ERR_HW_NO_PRESENT) {
                // The test can not be done, if LUN is not present
                lun_state &= ~(1 << lun);  // LUN test reseted
                continue;
            }
            lun_state |= (1 << lun);  // LUN test is done.
            print_dbg("\r\nfail");
            // ui_test_finish(false); // Test fail
            continue;
        }
        // Check if LUN has been already tested
        if (lun_state & (1 << lun)) { continue; }

        // WRITE SCENES
        char filename[13];
        strcpy(filename, "tt00s.txt");

        print_dbg("\r\nwriting scenes");
        strcpy(input_buffer, "WRITE");
        region_fill(&line[0], 0);
        font_string_region_clip_tab(&line[0], input_buffer, 2, 0, 0xa, 0);
        region_draw(&line[0]);

        for (int i = 0; i < SCENE_SLOTS; i++) {
            scene_state_t scene;
            ss_init(&scene);

            char text[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
            memset(text, 0, SCENE_TEXT_LINES * SCENE_TEXT_CHARS);

            strcat(input_buffer, ".");
            region_fill(&line[0], 0);
            font_string_region_clip_tab(&line[0], input_buffer, 2, 0, 0xa, 0);
            region_draw(&line[0]);

            flash_read(i, &scene, &text);

            if (!nav_file_create((FS_STRING)filename)) {
                if (fs_g_status != FS_ERR_FILE_EXIST) {
                    if (fs_g_status == FS_LUN_WP) {
                        // Test can be done only on no write protected
                        // device
                        continue;
                    }
                    lun_state |= (1 << lun);  // LUN test is done.
                    print_dbg("\r\nfail");
                    continue;
                }
            }
            if (!file_open(FOPEN_MODE_W)) {
                if (fs_g_status == FS_LUN_WP) {
                    // Test can be done only on no write protected
                    // device
                    continue;
                }
                lun_state |= (1 << lun);  // LUN test is done.
                print_dbg("\r\nfail");
                continue;
            }

            char blank = 0;
            for (int l = 0; l < SCENE_TEXT_LINES; l++) {
                if (strlen(text[l])) {
                    file_write_buf((uint8_t*)text[l], strlen(text[l]));
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
                    file_putc(s + 49);

                for (int l = 0; l < ss_get_script_len(&scene, s); l++) {
                    file_putc('\n');
                    print_command(ss_get_script_command(&scene, s, l), input);
                    file_write_buf((uint8_t*)input, strlen(input));
                }
            }

            file_putc('\n');
            file_putc('\n');
            file_putc('#');
            file_putc('P');
            file_putc('\n');

            for (int b = 0; b < 4; b++) {
                itoa(ss_get_pattern_len(&scene, b), input, 10);
                file_write_buf((uint8_t*)input, strlen(input));
                if (b == 3)
                    file_putc('\n');
                else
                    file_putc('\t');
            }

            for (int b = 0; b < 4; b++) {
                itoa(ss_get_pattern_wrap(&scene, b), input, 10);
                file_write_buf((uint8_t*)input, strlen(input));
                if (b == 3)
                    file_putc('\n');
                else
                    file_putc('\t');
            }

            for (int b = 0; b < 4; b++) {
                itoa(ss_get_pattern_start(&scene, b), input, 10);
                file_write_buf((uint8_t*)input, strlen(input));
                if (b == 3)
                    file_putc('\n');
                else
                    file_putc('\t');
            }

            for (int b = 0; b < 4; b++) {
                itoa(ss_get_pattern_end(&scene, b), input, 10);
                file_write_buf((uint8_t*)input, strlen(input));
                if (b == 3)
                    file_putc('\n');
                else
                    file_putc('\t');
            }

            file_putc('\n');

            for (int l = 0; l < 64; l++) {
                for (int b = 0; b < 4; b++) {
                    itoa(ss_get_pattern_val(&scene, b, l), input, 10);
                    file_write_buf((uint8_t*)input, strlen(input));
                    if (b == 3)
                        file_putc('\n');
                    else
                        file_putc('\t');
                }
            }

            file_close();
            lun_state |= (1 << lun);  // LUN test is done.

            if (filename[3] == '9') {
                filename[3] = '0';
                filename[2]++;
            }
            else
                filename[3]++;

            print_dbg(".");
        }

        nav_filelist_reset();


        // READ SCENES
        strcpy(filename, "tt00.txt");
        print_dbg("\r\nreading scenes...");

        strcpy(input_buffer, "READ");
        region_fill(&line[1], 0);
        font_string_region_clip_tab(&line[1], input_buffer, 2, 0, 0xa, 0);
        region_draw(&line[1]);

        for (int i = 0; i < SCENE_SLOTS; i++) {
            scene_state_t scene;
            ss_init(&scene);
            char text[SCENE_TEXT_LINES][SCENE_TEXT_CHARS];
            memset(text, 0, SCENE_TEXT_LINES * SCENE_TEXT_CHARS);

            strcat(input_buffer, ".");
            region_fill(&line[1], 0);
            font_string_region_clip_tab(&line[1], input_buffer, 2, 0, 0xa, 0);
            region_draw(&line[1]);
            if (nav_filelist_findname(filename, 0)) {
                print_dbg("\r\nfound: ");
                print_dbg(filename);
                if (!file_open(FOPEN_MODE_R))
                    print_dbg("\r\ncan't open");
                else {
                    char c;
                    uint8_t l = 0;
                    uint8_t p = 0;
                    int8_t s = 99;
                    uint8_t b = 0;
                    uint16_t num = 0;
                    int8_t neg = 1;

                    char input[32];
                    memset(input, 0, sizeof(input));

                    while (!file_eof() && s != -1) {
                        c = toupper(file_getc());
                        // print_dbg_char(c);

                        if (c == '#') {
                            if (!file_eof()) {
                                c = toupper(file_getc());
                                // print_dbg_char(c);

                                if (c == 'M')
                                    s = 8;
                                else if (c == 'I')
                                    s = 9;
                                else if (c == 'P')
                                    s = 10;
                                else {
                                    s = c - 49;
                                    if (s < 0 || s > 7) s = -1;
                                }

                                l = 0;
                                p = 0;

                                if (!file_eof()) c = toupper(file_getc());
                            }
                            else
                                s = -1;

                            // print_dbg("\r\nsection: ");
                            // print_dbg_ulong(s);
                        }
                        // SCENE TEXT
                        else if (s == 99) {
                            if (c == '\n') {
                                l++;
                                p = 0;
                            }
                            else {
                                if (l < SCENE_TEXT_LINES &&
                                    p < SCENE_TEXT_CHARS) {
                                    text[l][p] = c;
                                    p++;
                                }
                            }
                        }
                        // SCRIPTS
                        else if (s >= 0 && s <= 9) {
                            if (c == '\n') {
                                if (p && l < SCRIPT_MAX_COMMANDS) {
                                    tele_command_t temp;
                                    error_t status;
                                    char error_msg[TELE_ERROR_MSG_LENGTH];
                                    status = parse(input, &temp, error_msg);

                                    if (status == E_OK) {
                                        status = validate(&temp, error_msg);

                                        if (status == E_OK) {
                                            ss_overwrite_script_command(
                                                &scene, s, l, &temp);
                                            l++;
                                        }
                                        else {
                                            print_dbg("\r\nvalidate: ");
                                            print_dbg(tele_error(status));
                                            print_dbg(" >> ");
                                            print_dbg("\r\nINPUT: ");
                                            print_dbg(input);
                                        }
                                    }
                                    else {
                                        print_dbg("\r\nERROR: ");
                                        print_dbg(tele_error(status));
                                        print_dbg(" >> ");
                                        print_dbg("\r\nINPUT: ");
                                        print_dbg(input);
                                    }

                                    memset(input, 0, sizeof(input));
                                    p = 0;
                                }
                            }
                            else {
                                if (p < 32) input[p] = c;
                                p++;
                            }
                        }
                        // PATTERNS
                        // tele_patterns[]. l wrap start end v[64]
                        else if (s == 10) {
                            if (c == '\n' || c == '\t') {
                                if (b < 4) {
                                    if (l > 3) {
                                        ss_set_pattern_val(&scene, b, l - 4,
                                                           neg * num);
                                        // print_dbg("\r\nset: ");
                                        // print_dbg_ulong(b);
                                        // print_dbg(" ");
                                        // print_dbg_ulong(l-4);
                                        // print_dbg(" ");
                                        // print_dbg_ulong(num);
                                    }
                                    else if (l == 0) {
                                        ss_set_pattern_len(&scene, b, num);
                                    }
                                    else if (l == 1) {
                                        ss_set_pattern_wrap(&scene, b, num);
                                    }
                                    else if (l == 2) {
                                        ss_set_pattern_start(&scene, b, num);
                                    }
                                    else if (l == 3) {
                                        ss_set_pattern_end(&scene, b, num);
                                    }
                                }

                                b++;
                                num = 0;
                                neg = 1;

                                if (c == '\n') {
                                    if (p) l++;
                                    if (l > 68) s = -1;
                                    b = 0;
                                    p = 0;
                                }
                            }
                            else {
                                if (c == '-')
                                    neg = -1;
                                else if (c >= '0' && c <= '9') {
                                    num = num * 10 + (c - 48);
                                    // print_dbg("\r\nnum: ");
                                    // print_dbg_ulong(num);
                                }
                                p++;
                            }
                        }
                    }


                    file_close();

                    flash_write(i, &scene, &text);
                }
            }

            nav_filelist_reset();

            if (filename[3] == '9') {
                filename[3] = '0';
                filename[2]++;
            }
            else
                filename[3]++;
        }
    }

    nav_exit();
}

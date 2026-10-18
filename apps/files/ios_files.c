#include "ios_files.h"
#include "framework/ui/fonts.h"
#include "framework/ui/ios_layout.h"
#include "framework/ui/ios_status_bar.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "services/filesystem.h"
#define FILES_ROOT_SIZE HACHIMI_FS_PATH_MAX
#define FILES_NAME_SIZE HACHIMI_FS_NAME_MAX
#define FILES_MAX_ENTRIES 256
#define FILES_PREVIEW_LIMIT 8192
#define IOS_ASSET(name) "A:assets/ios/" name

typedef hachimi_fs_entry_t file_entry_t;

typedef enum {
    FILE_DIALOG_NONE,
    FILE_DIALOG_NEW_FOLDER,
    FILE_DIALOG_NEW_FILE,
    FILE_DIALOG_EDIT_FILE,
    FILE_DIALOG_RENAME,
    FILE_DIALOG_DELETE,
} file_dialog_mode_t;

static lv_obj_t * files_root;
static lv_obj_t * files_list;
static lv_obj_t * files_path_label;
static lv_obj_t * files_count_label;
static lv_obj_t * files_search;
static lv_obj_t * files_keyboard;
static lv_obj_t * dialog_overlay;
static lv_obj_t * dialog_panel;
static lv_obj_t * dialog_title;
static lv_obj_t * dialog_message;
static lv_obj_t * dialog_name;
static lv_obj_t * dialog_content;
static lv_obj_t * dialog_primary;
static lv_obj_t * context_menu;
static lv_obj_t * context_rename;
static lv_obj_t * context_delete;
static const lv_font_t * files_font;
static const lv_font_t * files_small_font;
static const lv_font_t * files_heading_font;
static file_entry_t entries[FILES_MAX_ENTRIES];
static size_t entry_count;
static char preview_buffer[FILES_PREVIEW_LIMIT + 1];
static char root_path[FILES_ROOT_SIZE];
static char current_path[FILES_ROOT_SIZE];
static char selected_name[FILES_NAME_SIZE];
static bool selected_directory;
static file_dialog_mode_t dialog_mode;

static void files_refresh_list(void);
static void close_dialog(void);

static lv_obj_t * make_label(lv_obj_t * parent, const char * text,
                             lv_coord_t x, lv_coord_t y,
                             lv_coord_t width, lv_coord_t height,
                             const lv_font_t * font, lv_color_t color)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

static lv_obj_t * make_button(lv_obj_t * parent, const char * text,
                              lv_coord_t x, lv_coord_t y,
                              lv_coord_t width, lv_coord_t height,
                              lv_color_t background, lv_color_t foreground,
                              const lv_font_t * font)
{
    lv_obj_t * button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_t * label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, foreground, LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

static bool path_is_root(void)
{
    return strcmp(root_path, current_path) == 0;
}

static void set_path_label(void)
{
    const char * title = "本地存储";
    if(!path_is_root()) {
        const char * last = strrchr(current_path, '/');
        const char * backslash = strrchr(current_path, '\\');
        if(backslash != NULL && (last == NULL || backslash > last)) last = backslash;
        title = last == NULL ? current_path : last + 1;
    }
    lv_label_set_text(files_path_label, title);
}

static bool entry_matches_search(const char * name)
{
    const char * query = files_search == NULL ? "" : lv_textarea_get_text(files_search);
    return query[0] == '\0' || strstr(name, query) != NULL;
}

static void make_file_icon(lv_obj_t * parent, bool directory)
{
    lv_obj_t * badge = lv_obj_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_pos(badge, 10, 9);
    lv_obj_set_size(badge, 28, 28);
    lv_obj_set_style_radius(badge, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, directory ? lv_color_hex(0x2f9bff) :
                              lv_color_hex(0x8e8e93), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * icon = lv_label_create(badge);
    lv_label_set_text(icon, directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(icon);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
}

static void context_action_event_cb(lv_event_t * event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if(code != LV_EVENT_CLICKED) return;
    uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    lv_obj_add_flag(context_menu, LV_OBJ_FLAG_HIDDEN);
    if(action == 1) {
        dialog_mode = FILE_DIALOG_RENAME;
        lv_label_set_text(dialog_title, "重命名");
        lv_label_set_text(dialog_message, "");
        lv_obj_add_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(dialog_name, selected_name);
        lv_obj_clear_flag(dialog_name, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(dialog_content, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(dialog_panel, 400, 188);
        lv_obj_set_pos(dialog_panel, 200, 116);
        lv_obj_set_pos(dialog_name, 22, 64);
        lv_obj_set_pos(dialog_primary, 257, 126);
        lv_label_set_text(lv_obj_get_child(dialog_primary, 0), "完成");
        lv_obj_clear_flag(dialog_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(dialog_overlay);
    }
    else {
        dialog_mode = FILE_DIALOG_DELETE;
        lv_label_set_text(dialog_title, "删除项目");
        lv_label_set_text(dialog_message, selected_directory ?
                          "此文件夹及其中内容将被删除。此操作无法撤销。" :
                          "此文件将被删除。此操作无法撤销。");
        lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(dialog_name, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(dialog_content, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(dialog_panel, 420, 190);
        lv_obj_set_pos(dialog_panel, 190, 112);
        lv_obj_set_pos(dialog_message, 22, 62);
        lv_obj_set_pos(dialog_primary, 277, 128);
        lv_obj_set_style_bg_color(dialog_primary, lv_color_hex(0xff3b30), LV_PART_MAIN);
        lv_label_set_text(lv_obj_get_child(dialog_primary, 0), "删除");
        lv_obj_clear_flag(dialog_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(dialog_overlay);
    }
}

static void open_context_menu(const file_entry_t * entry, uint32_t row)
{
    snprintf(selected_name, sizeof(selected_name), "%s", entry->name);
    selected_directory = entry->directory;
    lv_obj_set_pos(context_menu, 630, 93 + (lv_coord_t)row * 44);
    if(93 + (lv_coord_t)row * 44 > 330) lv_obj_set_y(context_menu, 330);
    lv_obj_clear_flag(context_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(context_menu);
}

static void entry_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    file_entry_t * entry = lv_event_get_user_data(event);
    if(entry == NULL) return;
    if(entry->directory) {
        char next[FILES_ROOT_SIZE];
        if(!hachimi_fs_join(current_path, entry->name, next, sizeof(next))) return;
        snprintf(current_path, sizeof(current_path), "%s", next);
        files_refresh_list();
    }
    else {
        snprintf(selected_name, sizeof(selected_name), "%s", entry->name);
        selected_directory = false;
        char path[FILES_ROOT_SIZE];
        if(!hachimi_fs_join(current_path, entry->name, path, sizeof(path))) return;
        unsigned long long file_size = 0;
        if(hachimi_fs_get_size(path, &file_size) && file_size > FILES_PREVIEW_LIMIT) {
            snprintf(selected_name, sizeof(selected_name), "%s", entry->name);
            lv_label_set_text(dialog_title, "文件较大");
            lv_label_set_text(dialog_message, "此文件超过 8 KB，暂不支持在文件应用中编辑。");
            lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(dialog_name, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(dialog_content, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(dialog_panel, 420, 190);
            lv_obj_set_pos(dialog_panel, 190, 112);
            lv_obj_set_pos(dialog_message, 22, 62);
            lv_obj_set_pos(dialog_primary, 277, 128);
            lv_obj_set_style_bg_color(dialog_primary, lv_color_hex(0x007aff), LV_PART_MAIN);
            lv_label_set_text(lv_obj_get_child(dialog_primary, 0), "好");
            dialog_mode = FILE_DIALOG_NONE;
            lv_obj_clear_flag(dialog_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(dialog_overlay);
            return;
        }
        size_t length = 0;
        if(!hachimi_fs_read_text(path, preview_buffer, sizeof(preview_buffer), &length)) return;
        bool binary = memchr(preview_buffer, '\0', length) != NULL;
        if(binary) {
            dialog_mode = FILE_DIALOG_RENAME;
            lv_label_set_text(dialog_title, "无法预览");
            lv_label_set_text(dialog_message, "此文件类型暂不支持预览，可在更多操作中重命名或删除。");
            lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(dialog_name, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(dialog_content, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(dialog_panel, 420, 190);
            lv_obj_set_pos(dialog_panel, 190, 112);
            lv_obj_set_pos(dialog_message, 22, 62);
            lv_obj_set_pos(dialog_primary, 277, 128);
            lv_obj_set_style_bg_color(dialog_primary, lv_color_hex(0x007aff), LV_PART_MAIN);
            lv_label_set_text(lv_obj_get_child(dialog_primary, 0), "好");
            lv_obj_clear_flag(dialog_overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(dialog_overlay);
            dialog_mode = FILE_DIALOG_NONE;
            return;
        }
        preview_buffer[length] = '\0';
        lv_textarea_set_text(dialog_name, entry->name);
        lv_textarea_set_text(dialog_content, preview_buffer);
        lv_label_set_text(dialog_title, "编辑文件");
        lv_label_set_text(dialog_message, "");
        lv_obj_add_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(dialog_name, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(dialog_content, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(dialog_panel, 500, 246);
        lv_obj_set_pos(dialog_panel, 150, 72);
        lv_obj_set_pos(dialog_name, 22, 60);
        lv_obj_set_pos(dialog_content, 22, 100);
        lv_obj_set_pos(dialog_primary, 357, 190);
        lv_obj_set_style_bg_color(dialog_primary, lv_color_hex(0x007aff), LV_PART_MAIN);
        lv_label_set_text(lv_obj_get_child(dialog_primary, 0), "存储");
        dialog_mode = FILE_DIALOG_EDIT_FILE;
        lv_obj_clear_flag(dialog_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(dialog_overlay);
    }
}

static void entry_more_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    file_entry_t * entry = lv_event_get_user_data(event);
    if(entry == NULL) return;
    uint32_t row = (uint32_t)(entry - entries);
    open_context_menu(entry, row);
}

static void format_size(unsigned long long size, char * text, size_t capacity)
{
    if(size < 1024) snprintf(text, capacity, "%llu B", size);
    else if(size < 1024 * 1024) snprintf(text, capacity, "%.1f KB", (double)size / 1024.0);
    else snprintf(text, capacity, "%.1f MB", (double)size / (1024.0 * 1024.0));
}

static void clear_list(void)
{
    uint32_t count = lv_obj_get_child_count(files_list);
    while(count > 0) {
        lv_obj_t * child = lv_obj_get_child(files_list, 0);
        if(child == NULL) break;
        lv_obj_delete(child);
        count--;
    }
}

static void files_refresh_list(void)
{
    if(files_list == NULL) return;
    clear_list();
    lv_obj_add_flag(context_menu, LV_OBJ_FLAG_HIDDEN);
    set_path_label();
    entry_count = 0;

    if(!hachimi_fs_list(current_path, entries, FILES_MAX_ENTRIES, &entry_count)) {
        lv_label_set_text(files_count_label, "无法读取此目录，请检查文件权限。");
        return;
    }
    size_t visible = 0;
    for(size_t i = 0; i < entry_count; i++) {
        file_entry_t * entry = &entries[i];
        if(!entry_matches_search(entry->name)) continue;
        lv_obj_t * row = lv_obj_create(files_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 530, 46);
        lv_obj_set_style_bg_color(row, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, entry_event_cb, LV_EVENT_CLICKED, entry);
        make_file_icon(row, entry->directory);
        make_label(row, entry->name, 48, 5, 300, 20, files_font, lv_color_hex(0x202124));
        char detail[48];
        if(entry->directory) snprintf(detail, sizeof(detail), "文件夹");
        else format_size(entry->size, detail, sizeof(detail));
        make_label(row, detail, 48, 25, 300, 16, files_small_font, lv_color_hex(0x8a8a90));
        lv_obj_t * more = lv_button_create(row);
        lv_obj_set_pos(more, 478, 8);
        lv_obj_set_size(more, 40, 30);
        lv_obj_set_style_bg_opa(more, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(more, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(more, entry_more_event_cb, LV_EVENT_CLICKED, entry);
        lv_obj_t * more_label = lv_label_create(more);
        lv_label_set_text(more_label, "•••");
        lv_obj_set_style_text_font(more_label, files_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(more_label, lv_color_hex(0x7b7b82), LV_PART_MAIN);
        lv_obj_center(more_label);
        visible++;
    }

    if(visible == 0) {
        make_label(files_list, lv_textarea_get_text(files_search)[0] == '\0' ?
                   "此文件夹为空" : "没有匹配的项目",
                   0, 148, 530, 24, files_font, lv_color_hex(0x85858b));
    }

    char count_text[64];
    snprintf(count_text, sizeof(count_text), "%u 项 · 本地存储", (unsigned)visible);
    lv_label_set_text(files_count_label, count_text);
    lv_obj_scroll_to_y(files_list, 0, LV_ANIM_OFF);
}

void ios_files_refresh(void)
{
    if(files_root == NULL) return;
    files_refresh_list();
}

static void close_dialog(void)
{
    lv_obj_add_flag(dialog_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(files_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(files_keyboard, NULL);
    dialog_mode = FILE_DIALOG_NONE;
    lv_obj_set_style_bg_color(dialog_primary, lv_color_hex(0x007aff), LV_PART_MAIN);
}

static void dialog_cancel_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED) close_dialog();
}

static void dialog_primary_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if(dialog_mode == FILE_DIALOG_NONE) {
        close_dialog();
        return;
    }

    char path[FILES_ROOT_SIZE];
    if(dialog_mode == FILE_DIALOG_DELETE) {
        if(!hachimi_fs_join(current_path, selected_name, path, sizeof(path)) ||
           !hachimi_fs_remove(path)) {
            lv_label_set_text(dialog_message, "无法删除此项目，请确认文件权限后重试。");
            return;
        }
    }
    else {
        const char * name = lv_textarea_get_text(dialog_name);
        if(!hachimi_fs_valid_name(name)) {
            lv_label_set_text(dialog_message, "名称无效，请输入不含路径分隔符的名称。");
            lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        char new_path[FILES_ROOT_SIZE];
        if(!hachimi_fs_join(current_path, name, new_path, sizeof(new_path))) return;
        if(dialog_mode == FILE_DIALOG_NEW_FOLDER) {
            if(hachimi_fs_exists(new_path)) {
                lv_label_set_text(dialog_message, "此位置已存在同名项目。");
                lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
                return;
            }
            if(!hachimi_fs_mkdir(new_path)) {
                lv_label_set_text(dialog_message, "无法创建文件夹，请检查文件权限。");
                lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
                return;
            }
        }
        else if(dialog_mode == FILE_DIALOG_NEW_FILE) {
            if(hachimi_fs_exists(new_path)) {
                lv_label_set_text(dialog_message, "此位置已存在同名项目。");
                lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
                return;
            }
            const char * content = lv_textarea_get_text(dialog_content);
            bool success = hachimi_fs_write_text(new_path, content, true);
            if(!success) {
                lv_label_set_text(dialog_message, "写入文件失败。");
                lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
                return;
            }
        }
        else if(dialog_mode == FILE_DIALOG_RENAME) {
            char old_path[FILES_ROOT_SIZE];
            if(!hachimi_fs_join(current_path, selected_name, old_path, sizeof(old_path))) return;
            if(strcmp(name, selected_name) != 0 && hachimi_fs_exists(new_path)) {
                lv_label_set_text(dialog_message, "此位置已存在同名项目。");
                lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
                return;
            }
            if(strcmp(name, selected_name) != 0 && !hachimi_fs_rename(old_path, new_path)) {
                lv_label_set_text(dialog_message, "无法重命名此项目。");
                lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
                return;
            }
        }
        else if(dialog_mode == FILE_DIALOG_EDIT_FILE) {
            char old_path[FILES_ROOT_SIZE];
            if(!hachimi_fs_join(current_path, selected_name, old_path, sizeof(old_path))) return;
            if(strcmp(name, selected_name) != 0 && hachimi_fs_exists(new_path)) {
                lv_label_set_text(dialog_message, "此位置已存在同名项目。");
                lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
                return;
            }
            const char * content = lv_textarea_get_text(dialog_content);
            bool success = hachimi_fs_write_text(old_path, content, false);
            if(!success) {
                lv_label_set_text(dialog_message, "保存文件失败。");
                lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
                return;
            }
            if(strcmp(name, selected_name) != 0 && !hachimi_fs_rename(old_path, new_path)) {
                lv_label_set_text(dialog_message, "内容已保存，但重命名失败。");
                lv_obj_clear_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
                return;
            }
        }
    }

    close_dialog();
    files_refresh_list();
}

static void dialog_keyboard_event_cb(lv_event_t * event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(files_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(files_keyboard, NULL);
    }
}

static void dialog_textarea_event_cb(lv_event_t * event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(files_keyboard, lv_event_get_target(event));
        lv_obj_clear_flag(files_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(files_keyboard);
    }
}

static void open_dialog(file_dialog_mode_t mode)
{
    dialog_mode = mode;
    lv_label_set_text(dialog_message, "");
    lv_obj_add_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(dialog_name, "");
    lv_textarea_set_text(dialog_content, "");
    lv_obj_add_flag(dialog_content, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(dialog_name, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(dialog_primary, lv_color_hex(0x007aff), LV_PART_MAIN);
    lv_obj_set_size(dialog_panel, 400, 188);
    lv_obj_set_pos(dialog_panel, 200, 116);
    lv_obj_set_pos(dialog_name, 22, 64);
    lv_obj_set_pos(dialog_primary, 257, 126);
    if(mode == FILE_DIALOG_NEW_FOLDER) {
        lv_label_set_text(dialog_title, "新建文件夹");
        lv_label_set_text(lv_obj_get_child(dialog_primary, 0), "创建");
    }
    else {
        lv_label_set_text(dialog_title, "新建文件");
        lv_label_set_text(lv_obj_get_child(dialog_primary, 0), "创建");
        lv_obj_clear_flag(dialog_content, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(dialog_panel, 500, 246);
        lv_obj_set_pos(dialog_panel, 150, 72);
        lv_obj_set_pos(dialog_name, 22, 60);
        lv_obj_set_pos(dialog_content, 22, 100);
        lv_obj_set_pos(dialog_primary, 357, 190);
    }
    lv_obj_clear_flag(dialog_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(dialog_overlay);
    lv_obj_move_foreground(files_keyboard);
    lv_obj_add_state(dialog_name, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(files_keyboard, dialog_name);
}

static void new_folder_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED) open_dialog(FILE_DIALOG_NEW_FOLDER);
}

static void new_file_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) == LV_EVENT_CLICKED) open_dialog(FILE_DIALOG_NEW_FILE);
}

static void back_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED || path_is_root()) return;
    char * slash = strrchr(current_path, '/');
    char * backslash = strrchr(current_path, '\\');
    if(backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    if(slash == NULL || slash < current_path + strlen(root_path))
        snprintf(current_path, sizeof(current_path), "%s", root_path);
    else if(slash == current_path || (slash == current_path + 2 && current_path[1] == ':'))
        slash[1] = '\0';
    else
        *slash = '\0';
    files_refresh_list();
}

static void location_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    snprintf(current_path, sizeof(current_path), "%s", root_path);
    if(files_search != NULL) lv_textarea_set_text(files_search, "");
    files_refresh_list();
}

static void search_event_cb(lv_event_t * event)
{
    if(lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) files_refresh_list();
    else if(lv_event_get_code(event) == LV_EVENT_FOCUSED ||
            lv_event_get_code(event) == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(files_keyboard, files_search);
        lv_obj_clear_flag(files_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(files_keyboard);
    }
}

static void initialize_root_path(void)
{
    hachimi_fs_home(root_path, sizeof(root_path));
    snprintf(current_path, sizeof(current_path), "%s", root_path);
}


static void files_deleted(lv_event_t * event)
{
    (void)event;
    files_root = files_list = files_search = NULL;
    dialog_overlay = files_keyboard = context_menu = NULL;
}

lv_obj_t * ios_files_create(lv_obj_t * parent, const lv_font_t * font,
                            const lv_font_t * small_font, const lv_font_t * heading_font)
{
    files_font = font;
    files_small_font = small_font;
    files_heading_font = heading_font;
    initialize_root_path();

    files_root = lv_obj_create(parent);
    lv_obj_add_event_cb(files_root, files_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_remove_style_all(files_root);
    lv_obj_set_pos(files_root, 0, 0);
    lv_obj_set_size(files_root, 800, 480);
    lv_obj_set_style_bg_color(files_root, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(files_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(files_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * sidebar = lv_obj_create(files_root);
    lv_obj_remove_style_all(sidebar);
    /* Match Settings: the sidebar color also continues behind the status bar. */
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_size(sidebar, 218, 480);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0xe2e6ee), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sidebar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * search_box = lv_obj_create(sidebar);
    lv_obj_remove_style_all(search_box);
    lv_obj_set_pos(search_box, 10, IOS_APP_CONTENT_SAFE_TOP);
    lv_obj_set_size(search_box, 198, 30);
    lv_obj_set_style_radius(search_box, 15, LV_PART_MAIN);
    lv_obj_set_style_bg_color(search_box, lv_color_hex(0xeff2f8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(search_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(search_box, lv_color_hex(0xd6dce5), LV_PART_MAIN);
    lv_obj_set_style_border_width(search_box, 1, LV_PART_MAIN);
    lv_obj_clear_flag(search_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * search_icon = lv_obj_create(search_box);
    lv_obj_remove_style_all(search_icon);
    lv_obj_set_pos(search_icon, 10, 8);
    lv_obj_set_size(search_icon, 10, 10);
    lv_obj_set_style_radius(search_icon, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(search_icon, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(search_icon, lv_color_hex(0x5b626c), LV_PART_MAIN);
    lv_obj_clear_flag(search_icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    static const lv_point_precise_t search_handle_points[] = {{0, 0}, {5, 5}};
    lv_obj_t * search_handle = lv_line_create(search_box);
    lv_line_set_points(search_handle, search_handle_points, 2);
    lv_obj_set_pos(search_handle, 18, 16);
    lv_obj_set_style_line_width(search_handle, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(search_handle, lv_color_hex(0x5b626c), LV_PART_MAIN);
    files_search = lv_textarea_create(search_box);
    lv_obj_remove_style_all(files_search);
    lv_obj_set_pos(files_search, 31, 4);
    lv_obj_set_size(files_search, 153, 23);
    lv_obj_set_style_text_font(files_search, files_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(files_search, lv_color_hex(0x30333a), LV_PART_MAIN);
    lv_obj_set_style_text_color(files_search, lv_color_hex(0x818893), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(files_search, lv_color_hex(0x30333a), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(files_search, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(files_search, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_textarea_set_one_line(files_search, true);
    lv_obj_set_height(files_search, 23);
    lv_obj_add_flag(files_search, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_textarea_set_max_length(files_search, 64);
    lv_textarea_set_placeholder_text(files_search, "搜索");
    lv_textarea_set_text(files_search, "");
    lv_obj_set_scrollbar_mode(files_search, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(files_search, search_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(search_box, search_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * app_icon = lv_image_create(sidebar);
    lv_image_set_src(app_icon, IOS_ASSET("files.png"));
    lv_image_set_pivot(app_icon, 0, 0);
    lv_image_set_scale(app_icon, 144);
    lv_image_set_inner_align(app_icon, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_set_pos(app_icon, 18, 108);
    lv_obj_clear_flag(app_icon, LV_OBJ_FLAG_CLICKABLE);
    make_label(sidebar, "文件", 65, 110, 140, 24, files_heading_font, lv_color_hex(0x17181b));
    make_label(sidebar, "本地存储", 65, 134, 140, 20, files_small_font, lv_color_hex(0x737c89));

    lv_obj_t * browse = lv_obj_create(sidebar);
    lv_obj_remove_style_all(browse);
    lv_obj_set_pos(browse, 10, 184);
    lv_obj_set_size(browse, 198, 36);
    lv_obj_set_style_radius(browse, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(browse, lv_color_hex(0xcbd1db), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(browse, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(browse, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * browse_icon = lv_label_create(browse);
    lv_label_set_text(browse_icon, LV_SYMBOL_DIRECTORY);
    lv_obj_set_pos(browse_icon, 12, 9);
    lv_obj_set_style_text_color(browse_icon, lv_color_hex(0x007aff), LV_PART_MAIN);
    lv_obj_set_style_text_font(browse_icon, &lv_font_montserrat_14, LV_PART_MAIN);
    make_label(browse, "浏览", 42, 7, 142, 22, files_font, lv_color_hex(0x202124));

    make_label(sidebar, "位置", 18, 236, 180, 18, files_small_font, lv_color_hex(0x85858b));
    lv_obj_t * location = lv_obj_create(sidebar);
    lv_obj_remove_style_all(location);
    lv_obj_set_pos(location, 10, 260);
    lv_obj_set_size(location, 198, 38);
    lv_obj_set_style_radius(location, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(location, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(location, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(location, location_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * location_icon = lv_label_create(location);
    lv_label_set_text(location_icon, LV_SYMBOL_DRIVE);
    lv_obj_set_pos(location_icon, 12, 10);
    lv_obj_set_style_text_font(location_icon, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(location_icon, lv_color_hex(0x8e8e93), LV_PART_MAIN);
    make_label(location, "本地存储", 42, 8, 140, 22, files_font, lv_color_hex(0x202124));

    lv_obj_t * main_panel = lv_obj_create(files_root);
    lv_obj_remove_style_all(main_panel);
    lv_obj_set_pos(main_panel, 218, IOS_APP_CONTENT_PANEL_TOP);
    lv_obj_set_size(main_panel, 582, 452);
    lv_obj_set_style_bg_color(main_panel, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(main_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(main_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * back = lv_button_create(main_panel);
    lv_obj_remove_style_all(back);
    lv_obj_set_pos(back, 10, IOS_APP_CONTENT_PANEL_LOCAL_TOP);
    lv_obj_set_size(back, 32, 30);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * back_glyph = lv_label_create(back);
    lv_label_set_text(back_glyph, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_glyph, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_glyph, lv_color_hex(0x007aff), LV_PART_MAIN);
    lv_obj_center(back_glyph);
    files_path_label = make_label(main_panel, "本地存储", 48, IOS_APP_CONTENT_PANEL_LOCAL_TOP, 185, 28,
                                  files_heading_font, lv_color_hex(0x202124));
    lv_obj_t * new_folder = make_button(main_panel, "新建文件夹", 331,
                                         IOS_APP_CONTENT_PANEL_LOCAL_TOP, 106, 32,
                                         lv_color_white(), lv_color_hex(0x007aff), files_small_font);
    lv_obj_add_event_cb(new_folder, new_folder_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * new_file = make_button(main_panel, "+ 文件", 444,
                                       IOS_APP_CONTENT_PANEL_LOCAL_TOP, 76, 32,
                                       lv_color_hex(0x007aff), lv_color_white(), files_small_font);
    lv_obj_add_event_cb(new_file, new_file_event_cb, LV_EVENT_CLICKED, NULL);

    files_list = lv_obj_create(main_panel);
    lv_obj_remove_style_all(files_list);
    lv_obj_set_pos(files_list, 14, IOS_APP_CONTENT_PANEL_LOCAL_TOP + 42);
    lv_obj_set_size(files_list, 554, 348);
    lv_obj_set_style_bg_color(files_list, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(files_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(files_list, 15, LV_PART_MAIN);
    lv_obj_set_style_pad_all(files_list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(files_list, 3, LV_PART_MAIN);
    lv_obj_set_flex_flow(files_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(files_list, LV_SCROLLBAR_MODE_AUTO);

    files_count_label = make_label(main_panel, "0 项 · 本地存储", 20, 428, 520, 20,
                                   files_small_font, lv_color_hex(0x85858b));

    context_menu = lv_obj_create(files_root);
    lv_obj_remove_style_all(context_menu);
    lv_obj_set_size(context_menu, 158, 86);
    lv_obj_set_style_radius(context_menu, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(context_menu, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(context_menu, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(context_menu, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(context_menu, LV_OPA_20, LV_PART_MAIN);
    lv_obj_add_flag(context_menu, LV_OBJ_FLAG_HIDDEN);
    context_rename = make_button(context_menu, "重命名", 8, 7, 142, 32,
                                  lv_color_white(), lv_color_hex(0x202124), files_font);
    context_delete = make_button(context_menu, "删除", 8, 46, 142, 32,
                                  lv_color_white(), lv_color_hex(0xff3b30), files_font);
    lv_obj_add_event_cb(context_rename, context_action_event_cb, LV_EVENT_CLICKED, (void *)1);
    lv_obj_add_event_cb(context_delete, context_action_event_cb, LV_EVENT_CLICKED, (void *)2);

    dialog_overlay = lv_obj_create(files_root);
    lv_obj_remove_style_all(dialog_overlay);
    lv_obj_set_size(dialog_overlay, 800, 480);
    lv_obj_set_style_bg_color(dialog_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dialog_overlay, 95, LV_PART_MAIN);
    lv_obj_clear_flag(dialog_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dialog_overlay, LV_OBJ_FLAG_HIDDEN);

    dialog_panel = lv_obj_create(dialog_overlay);
    lv_obj_remove_style_all(dialog_panel);
    lv_obj_set_size(dialog_panel, 400, 188);
    lv_obj_set_pos(dialog_panel, 200, 116);
    lv_obj_set_style_radius(dialog_panel, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dialog_panel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dialog_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(dialog_panel, 18, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(dialog_panel, LV_OPA_30, LV_PART_MAIN);
    dialog_title = make_label(dialog_panel, "新建文件夹", 22, 14, 350, 28,
                              files_heading_font, lv_color_hex(0x202124));
    dialog_message = make_label(dialog_panel, "", 22, 62, 360, 36,
                                files_small_font, lv_color_hex(0x85858b));
    lv_obj_add_flag(dialog_message, LV_OBJ_FLAG_HIDDEN);

    dialog_name = lv_textarea_create(dialog_panel);
    lv_obj_remove_style_all(dialog_name);
    lv_obj_set_pos(dialog_name, 22, 64);
    lv_obj_set_size(dialog_name, 356, 36);
    lv_obj_set_style_radius(dialog_name, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dialog_name, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dialog_name, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(dialog_name, files_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(dialog_name, lv_color_hex(0x202124), LV_PART_MAIN);
    lv_textarea_set_one_line(dialog_name, true);
    lv_textarea_set_max_length(dialog_name, FILES_NAME_SIZE - 1);
    lv_obj_add_event_cb(dialog_name, dialog_textarea_event_cb, LV_EVENT_ALL, NULL);

    dialog_content = lv_textarea_create(dialog_panel);
    lv_obj_remove_style_all(dialog_content);
    lv_obj_set_pos(dialog_content, 22, 100);
    lv_obj_set_size(dialog_content, 456, 78);
    lv_obj_set_style_radius(dialog_content, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dialog_content, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dialog_content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(dialog_content, files_small_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(dialog_content, lv_color_hex(0x202124), LV_PART_MAIN);
    lv_textarea_set_max_length(dialog_content, FILES_PREVIEW_LIMIT - 1);
    lv_textarea_set_placeholder_text(dialog_content, "输入文件内容");
    lv_obj_add_event_cb(dialog_content, dialog_textarea_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * cancel = make_button(dialog_panel, "取消", 166, 126, 82, 38,
                                    lv_color_hex(0xf2f2f7), lv_color_hex(0x007aff), files_font);
    lv_obj_add_event_cb(cancel, dialog_cancel_event_cb, LV_EVENT_CLICKED, NULL);
    dialog_primary = make_button(dialog_panel, "创建", 257, 126, 122, 38,
                                 lv_color_hex(0x007aff), lv_color_white(), files_font);
    lv_obj_add_event_cb(dialog_primary, dialog_primary_event_cb, LV_EVENT_CLICKED, NULL);

    files_keyboard = lv_keyboard_create(files_root);
    lv_obj_set_pos(files_keyboard, 40, 300);
    lv_obj_set_size(files_keyboard, 720, 166);
    lv_obj_set_style_align(files_keyboard, LV_ALIGN_TOP_LEFT, LV_PART_MAIN);
    lv_obj_add_event_cb(files_keyboard, dialog_keyboard_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(files_keyboard, LV_OBJ_FLAG_HIDDEN);

    /* Match Settings' topmost status-bar layer so panels cannot cover its lower edge. */
    ios_status_bar_create(files_root, lv_color_hex(0x202124));

    lv_obj_add_flag(files_root, LV_OBJ_FLAG_HIDDEN);
    files_refresh_list();
    return files_root;
}


static lv_obj_t * files_app_create(lv_obj_t * parent)
{
    return ios_files_create(parent, hachimi_ui_font(14), hachimi_ui_font(12), hachimi_ui_font(18));
}

static void files_app_open(lv_obj_t * view) { (void)view; ios_files_refresh(); }
static void files_app_close(lv_obj_t * view) { (void)view; close_dialog(); lv_obj_add_flag(context_menu, LV_OBJ_FLAG_HIDDEN); }

const hachimi_app_module_t hachimi_files_app = {
    "files", files_app_create, files_app_open, files_app_close, NULL, NULL
};

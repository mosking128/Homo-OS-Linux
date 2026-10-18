#include "books_app.h"

#include "framework/ui/fonts.h"
#include "framework/ui/ios_layout.h"
#include "framework/ui/ios_status_bar.h"
#include "services/filesystem.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOOKS_MAX_ENTRIES 256
#define BOOKS_PAGE_BYTES 4096
#define BOOKS_MAX_PAGES 2048
#define BOOKS_ROOT_SIZE HACHIMI_FS_PATH_MAX
#define IOS_ASSET(name) "A:assets/ios/" name
#define INK 0x202124
#define MUTED 0x85858b
#define BLUE 0x007aff
#define BOOK_GREEN 0x5b9a78

typedef struct {
    char name[HACHIMI_FS_NAME_MAX];
    unsigned long long size;
} book_entry_t;

static lv_obj_t *root;
static lv_obj_t *list_panel;
static lv_obj_t *list;
static lv_obj_t *count_label;
static lv_obj_t *search;
static lv_obj_t *keyboard;
static lv_obj_t *reader_panel;
static lv_obj_t *reader_title;
static lv_obj_t *reader_body;
static lv_obj_t *page_label;
static lv_obj_t *previous_button;
static lv_obj_t *next_button;
static const lv_font_t *book_font;
static const lv_font_t *small_font;
static const lv_font_t *heading_font;
static book_entry_t entries[BOOKS_MAX_ENTRIES];
static hachimi_fs_entry_t filesystem_entries[BOOKS_MAX_ENTRIES];
static size_t entry_count;
static char books_directory[BOOKS_ROOT_SIZE];
static char active_book[HACHIMI_FS_NAME_MAX];
static char page_buffer[BOOKS_PAGE_BYTES + 4];
static unsigned long long page_offsets[BOOKS_MAX_PAGES];
static unsigned page_index;
static unsigned page_count;

static void render_book_list(void);
static void render_reader_page(void);

static lv_obj_t *label(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                       lv_coord_t width, lv_coord_t height, const lv_font_t *font,
                       lv_color_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                        lv_coord_t width, lv_coord_t height, lv_color_t background,
                        lv_color_t foreground, const lv_font_t *font)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_radius(obj, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *caption = lv_label_create(obj);
    lv_label_set_text(caption, text);
    lv_obj_set_style_text_font(caption, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(caption, foreground, LV_PART_MAIN);
    lv_obj_center(caption);
    return obj;
}

static bool is_txt_file(const char *name)
{
    size_t length = strlen(name);
    return length >= 4 && name[length - 4] == '.' &&
           tolower((unsigned char)name[length - 3]) == 't' &&
           tolower((unsigned char)name[length - 2]) == 'x' &&
           tolower((unsigned char)name[length - 1]) == 't';
}

static void clear_list(void)
{
    while(lv_obj_get_child_count(list) > 0) lv_obj_delete(lv_obj_get_child(list, 0));
}

static void format_size(unsigned long long size, char *text, size_t capacity)
{
    if(size < 1024) snprintf(text, capacity, "%llu B", size);
    else if(size < 1024 * 1024) snprintf(text, capacity, "%.1f KB", (double)size / 1024.0);
    else snprintf(text, capacity, "%.1f MB", (double)size / (1024.0 * 1024.0));
}

static void open_book(const book_entry_t *book)
{
    char path[BOOKS_ROOT_SIZE];
    unsigned long long size = 0;
    if(!hachimi_fs_join(books_directory, book->name, path, sizeof(path)) ||
       !hachimi_fs_get_size(path, &size)) return;
    snprintf(active_book, sizeof(active_book), "%s", book->name);
    page_index = 0;
    page_count = 1;
    page_offsets[0] = 0;
    lv_label_set_text(reader_title, active_book);
    lv_obj_add_flag(list_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(reader_panel, LV_OBJ_FLAG_HIDDEN);
    render_reader_page();
}

static void entry_event_cb(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    book_entry_t *book = lv_event_get_user_data(event);
    if(book != NULL) open_book(book);
}

static void make_book_icon(lv_obj_t *parent)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_pos(badge, 10, 8);
    lv_obj_set_size(badge, 30, 30);
    lv_obj_set_style_radius(badge, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_hex(BOOK_GREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *icon = lv_label_create(badge);
    lv_label_set_text(icon, LV_SYMBOL_FILE);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(icon);
}

static void render_book_list(void)
{
    clear_list();
    entry_count = 0;
    if(!hachimi_fs_list(books_directory, filesystem_entries,
                        BOOKS_MAX_ENTRIES, &entry_count)) {
        lv_label_set_text(count_label, "无法读取图书目录，请检查目录权限。");
        return;
    }

    const char *query = lv_textarea_get_text(search);
    size_t visible = 0;
    for(size_t i = 0; i < entry_count; i++) {
        hachimi_fs_entry_t *entry = &filesystem_entries[i];
        if(entry->directory || !is_txt_file(entry->name)) continue;
        if(query[0] != '\0' && strstr(entry->name, query) == NULL) continue;
        entries[visible].size = entry->size;
        snprintf(entries[visible].name, sizeof(entries[visible].name), "%s", entry->name);
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 530, 48);
        lv_obj_set_style_bg_color(row, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, entry_event_cb, LV_EVENT_CLICKED, &entries[visible]);
        make_book_icon(row);
        label(row, entries[visible].name, 50, 5, 390, 21, book_font, lv_color_hex(INK));
        char detail[48];
        format_size(entries[visible].size, detail, sizeof(detail));
        label(row, detail, 50, 26, 390, 16, small_font, lv_color_hex(MUTED));
        lv_obj_t *chevron = lv_label_create(row);
        lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_pos(chevron, 498, 16);
        lv_obj_set_style_text_font(chevron, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(chevron, lv_color_hex(0xb0b0b5), LV_PART_MAIN);
        lv_obj_clear_flag(chevron, LV_OBJ_FLAG_CLICKABLE);
        visible++;
    }
    if(visible == 0) {
        const char *empty_text = query[0] == '\0' ? "此目录还没有 TXT 图书" : "没有匹配的图书";
        label(list, empty_text, 0, 148, 530, 24, book_font, lv_color_hex(MUTED));
    }
    char count_text[64];
    snprintf(count_text, sizeof(count_text), "%u 本图书 · TXT", (unsigned)visible);
    lv_label_set_text(count_label, count_text);
    lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
}

static bool utf8_continuation(unsigned char value)
{
    return (value & 0xc0) == 0x80;
}

static void render_reader_page(void)
{
    char path[BOOKS_ROOT_SIZE];
    unsigned long long file_size = 0;
    size_t length = 0;
    if(!hachimi_fs_join(books_directory, active_book, path, sizeof(path)) ||
       !hachimi_fs_get_size(path, &file_size) ||
       !hachimi_fs_read_text_range(path, page_offsets[page_index], page_buffer,
                                   sizeof(page_buffer), &length)) {
        lv_textarea_set_text(reader_body, "无法打开此 TXT 文件。");
        return;
    }
    unsigned long long next_offset = page_offsets[page_index] + length;
    if(next_offset < file_size) {
        if(length > 0) {
            size_t available = length;
            while(available > 0 && utf8_continuation((unsigned char)page_buffer[available - 1])) available--;
            if(available > 0) {
                unsigned char final_lead = (unsigned char)page_buffer[available - 1];
                size_t final_expected = final_lead < 0x80 ? 1 : (final_lead & 0xe0) == 0xc0 ? 2 :
                                        (final_lead & 0xf0) == 0xe0 ? 3 : (final_lead & 0xf8) == 0xf0 ? 4 : 1;
                size_t final_available = length - (available - 1);
                if(final_available < final_expected) length = available - 1;
            }
        }
        if(length == 0) {
            lv_textarea_set_text(reader_body, "文本编码无法识别。");
            return;
        }
        next_offset = page_offsets[page_index] + length;
    }
    page_buffer[length] = '\0';
    lv_textarea_set_text(reader_body, page_buffer);
    lv_obj_scroll_to_y(reader_body, 0, LV_ANIM_OFF);

    if(next_offset < file_size && page_index + 1 < BOOKS_MAX_PAGES) {
        if(page_index + 1 >= page_count) {
            page_offsets[page_index + 1] = next_offset;
            page_count = page_index + 2;
        }
        lv_obj_clear_state(next_button, LV_STATE_DISABLED);
    }
    else lv_obj_add_state(next_button, LV_STATE_DISABLED);
    if(page_index == 0) lv_obj_add_state(previous_button, LV_STATE_DISABLED);
    else lv_obj_clear_state(previous_button, LV_STATE_DISABLED);
    char status[64];
    snprintf(status, sizeof(status), "第 %u 页", page_index + 1);
    lv_label_set_text(page_label, status);
}

static void page_event_cb(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    intptr_t direction = (intptr_t)lv_event_get_user_data(event);
    if(direction < 0 && page_index > 0) page_index--;
    else if(direction > 0 && page_index + 1 < page_count) page_index++;
    else return;
    render_reader_page();
}

static void back_event_cb(lv_event_t *event)
{
    if(lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if(!lv_obj_has_flag(reader_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(reader_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(list_panel, LV_OBJ_FLAG_HIDDEN);
        render_book_list();
    }
}

static void search_event_cb(lv_event_t *event)
{
    if(lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) render_book_list();
}

static void initialize_books_directory(void)
{
    const char *configured = getenv("HACHIMI_BOOKS_DIR");
    if(configured != NULL && configured[0] != '\0') {
        snprintf(books_directory, sizeof(books_directory), "%s", configured);
    }
#ifdef _WIN32
    else {
        if(!hachimi_fs_home(books_directory, sizeof(books_directory))) snprintf(books_directory, sizeof(books_directory), ".");
        size_t used = strlen(books_directory);
        snprintf(books_directory + used, sizeof(books_directory) - used, "\\Books");
    }
#else
    else snprintf(books_directory, sizeof(books_directory), "/home/debian/hachimi-desktop/books");
#endif
    if(!hachimi_fs_is_directory(books_directory)) hachimi_fs_mkdir(books_directory);
}

static void books_deleted(lv_event_t *event)
{
    (void)event;
    root = list_panel = list = search = keyboard = reader_panel = reader_body = NULL;
}

static void search_focused_cb(lv_event_t *event)
{
    (void)event;
    lv_keyboard_set_textarea(keyboard, search);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard);
}

static void keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard, NULL);
    }
}

static lv_obj_t *books_create(lv_obj_t *parent)
{
    book_font = hachimi_ui_font(14);
    small_font = hachimi_ui_font(12);
    heading_font = hachimi_ui_font(18);
    initialize_books_directory();

    root = lv_obj_create(parent);
    lv_obj_add_event_cb(root, books_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_remove_style_all(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 800, 480);
    lv_obj_set_style_bg_color(root, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sidebar = lv_obj_create(root);
    lv_obj_remove_style_all(sidebar);
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_size(sidebar, 218, 480);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0xe2e6ee), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sidebar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *search_box = lv_obj_create(sidebar);
    lv_obj_remove_style_all(search_box);
    lv_obj_set_pos(search_box, 10, IOS_APP_CONTENT_SAFE_TOP);
    lv_obj_set_size(search_box, 198, 30);
    lv_obj_set_style_radius(search_box, 15, LV_PART_MAIN);
    lv_obj_set_style_bg_color(search_box, lv_color_hex(0xeff2f8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(search_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(search_box, lv_color_hex(0xd6dce5), LV_PART_MAIN);
    lv_obj_set_style_border_width(search_box, 1, LV_PART_MAIN);
    lv_obj_clear_flag(search_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *search_icon = lv_obj_create(search_box);
    lv_obj_remove_style_all(search_icon);
    lv_obj_set_pos(search_icon, 10, 8);
    lv_obj_set_size(search_icon, 10, 10);
    lv_obj_set_style_radius(search_icon, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(search_icon, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(search_icon, lv_color_hex(0x5b626c), LV_PART_MAIN);
    lv_obj_clear_flag(search_icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    static const lv_point_precise_t search_handle_points[] = {{0, 0}, {5, 5}};
    lv_obj_t *search_handle = lv_line_create(search_box);
    lv_line_set_points(search_handle, search_handle_points, 2);
    lv_obj_set_pos(search_handle, 18, 16);
    lv_obj_set_style_line_width(search_handle, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(search_handle, lv_color_hex(0x5b626c), LV_PART_MAIN);

    search = lv_textarea_create(search_box);
    lv_obj_remove_style_all(search);
    lv_obj_set_pos(search, 31, 4);
    lv_obj_set_size(search, 153, 23);
    lv_obj_set_style_text_font(search, book_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(search, lv_color_hex(0x30333a), LV_PART_MAIN);
    lv_obj_set_style_text_color(search, lv_color_hex(0x818893), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(search, lv_color_hex(0x30333a), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(search, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(search, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_textarea_set_one_line(search, true);
    lv_obj_set_height(search, 23);
    lv_obj_add_flag(search, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_textarea_set_max_length(search, 64);
    lv_textarea_set_placeholder_text(search, "搜索图书");
    lv_textarea_set_text(search, "");
    lv_obj_set_scrollbar_mode(search, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(search, search_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(search, search_focused_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(search, search_focused_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(search, search_focused_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(search_box, search_focused_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *app_icon = lv_image_create(sidebar);
    lv_image_set_src(app_icon, IOS_ASSET("books.png"));
    lv_image_set_pivot(app_icon, 0, 0);
    lv_image_set_scale(app_icon, 144);
    lv_image_set_inner_align(app_icon, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_set_pos(app_icon, 18, 108);
    lv_obj_clear_flag(app_icon, LV_OBJ_FLAG_CLICKABLE);
    label(sidebar, "图书", 65, 110, 140, 24, heading_font, lv_color_hex(0x17181b));
    label(sidebar, "TXT 书库", 65, 134, 140, 20, small_font, lv_color_hex(0x737c89));
    lv_obj_t *location = lv_obj_create(sidebar);
    lv_obj_remove_style_all(location);
    lv_obj_set_pos(location, 10, 184);
    lv_obj_set_size(location, 198, 38);
    lv_obj_set_style_radius(location, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(location, lv_color_hex(0xcbd1db), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(location, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(location, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *location_icon = lv_label_create(location);
    lv_label_set_text(location_icon, LV_SYMBOL_DIRECTORY);
    lv_obj_set_pos(location_icon, 12, 10);
    lv_obj_set_style_text_color(location_icon, lv_color_hex(BLUE), LV_PART_MAIN);
    lv_obj_set_style_text_font(location_icon, &lv_font_montserrat_14, LV_PART_MAIN);
    label(location, "我的图书", 42, 8, 140, 22, book_font, lv_color_hex(INK));
    list_panel = lv_obj_create(root);
    lv_obj_remove_style_all(list_panel);
    lv_obj_set_pos(list_panel, 218, IOS_APP_CONTENT_PANEL_TOP);
    lv_obj_set_size(list_panel, 582, 452);
    lv_obj_set_style_bg_color(list_panel, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(list_panel, LV_OBJ_FLAG_SCROLLABLE);
    label(list_panel, "我的图书", 20, IOS_APP_CONTENT_PANEL_LOCAL_TOP, 300, 28,
          heading_font, lv_color_hex(INK));
    list = lv_obj_create(list_panel);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 14, IOS_APP_CONTENT_PANEL_LOCAL_TOP + 42);
    lv_obj_set_size(list, 554, 348);
    lv_obj_set_style_bg_color(list, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(list, 15, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 3, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    count_label = label(list_panel, "0 本图书 · TXT", 20, 428, 520, 20, small_font, lv_color_hex(MUTED));

    reader_panel = lv_obj_create(root);
    lv_obj_remove_style_all(reader_panel);
    lv_obj_set_pos(reader_panel, 218, IOS_APP_CONTENT_PANEL_TOP);
    lv_obj_set_size(reader_panel, 582, 452);
    lv_obj_set_style_bg_color(reader_panel, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(reader_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(reader_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(reader_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back = button(reader_panel, LV_SYMBOL_LEFT, 12, IOS_APP_CONTENT_PANEL_LOCAL_TOP,
                            36, 30, lv_color_white(), lv_color_hex(BLUE), &lv_font_montserrat_14);
    lv_obj_add_event_cb(back, back_event_cb, LV_EVENT_CLICKED, NULL);
    reader_title = label(reader_panel, "", 58, IOS_APP_CONTENT_PANEL_LOCAL_TOP, 390, 30,
                         heading_font, lv_color_hex(INK));
    reader_body = lv_textarea_create(reader_panel);
    lv_obj_remove_style_all(reader_body);
    lv_obj_set_pos(reader_body, 14, IOS_APP_CONTENT_PANEL_LOCAL_TOP + 42);
    lv_obj_set_size(reader_body, 554, 306);
    lv_obj_set_style_radius(reader_body, 15, LV_PART_MAIN);
    lv_obj_set_style_bg_color(reader_body, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(reader_body, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(reader_body, 16, LV_PART_MAIN);
    lv_obj_set_style_text_font(reader_body, book_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(reader_body, lv_color_hex(INK), LV_PART_MAIN);
    lv_textarea_set_one_line(reader_body, false);
    lv_textarea_set_cursor_click_pos(reader_body, false);
    lv_obj_clear_flag(reader_body, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    page_label = label(reader_panel, "第 1 页", 250, 403, 90, 20, small_font, lv_color_hex(MUTED));
    previous_button = button(reader_panel, "上一页", 365, 397, 82, 32,
                             lv_color_white(), lv_color_hex(BLUE), book_font);
    next_button = button(reader_panel, "下一页", 455, 397, 82, 32,
                         lv_color_hex(BLUE), lv_color_white(), book_font);
    lv_obj_add_event_cb(previous_button, page_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_obj_add_event_cb(next_button, page_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_add_state(previous_button, LV_STATE_DISABLED);

    keyboard = lv_keyboard_create(root);
    lv_obj_set_pos(keyboard, 40, 300);
    lv_obj_set_size(keyboard, 720, 166);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    ios_status_bar_create(root, lv_color_hex(INK));
    render_book_list();
    return root;
}

static void books_open(lv_obj_t *view)
{
    (void)view;
    render_book_list();
}

static void books_close(lv_obj_t *view)
{
    (void)view;
    if(keyboard != NULL) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard, NULL);
    }
}

const hachimi_app_module_t hachimi_books_app = {
    "books", books_create, books_open, books_close, NULL, NULL
};

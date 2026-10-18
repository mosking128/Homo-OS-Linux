#include "settings_app.h"
#include "pages/ios_network.h"
#include "pages/ios_display.h"
#include "pages/ios_time.h"
#include "framework/ui/fonts.h"
#include "framework/ui/ios_layout.h"
#include "framework/ui/ios_status_bar.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#ifdef __linux__
#include <sys/statvfs.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define IOS_ASSET(name) "A:assets/ios/" name
#define DISPLAY_WIDTH 800
#define DISPLAY_HEIGHT 480

typedef enum {
    SETTINGS_PAGE_ABOUT,
    SETTINGS_PAGE_NETWORK,
    SETTINGS_PAGE_DISPLAY,
    SETTINGS_PAGE_TIME,
    SETTINGS_PAGE_COUNT,
} settings_page_t;

static lv_obj_t * settings_root;
static lv_obj_t * settings_search;
static lv_obj_t * settings_keyboard;
static lv_obj_t * settings_nav_buttons[SETTINGS_PAGE_COUNT];
static lv_obj_t * settings_nav_icons[SETTINGS_PAGE_COUNT];
static lv_obj_t * settings_nav_labels[SETTINGS_PAGE_COUNT];
static lv_obj_t * settings_pages[SETTINGS_PAGE_COUNT];
static lv_obj_t * settings_memory_value;
static lv_obj_t * settings_storage_value;

static const settings_page_t settings_page_ids[SETTINGS_PAGE_COUNT] = {
    SETTINGS_PAGE_ABOUT,
    SETTINGS_PAGE_NETWORK,
    SETTINGS_PAGE_DISPLAY,
    SETTINGS_PAGE_TIME,
};

static void settings_show_page(settings_page_t page);

static void format_capacity(char * output, size_t capacity,
                            unsigned long long total, unsigned long long available,
                            bool memory)
{
    if(memory) {
        snprintf(output, capacity, "%llu MiB 总量 · 可用 %llu MiB",
                 total / (1024ULL * 1024ULL), available / (1024ULL * 1024ULL));
    }
    else {
        snprintf(output, capacity, "%.1f GiB 总量 · 可用 %.1f GiB",
                 (double)total / (1024.0 * 1024.0 * 1024.0),
                 (double)available / (1024.0 * 1024.0 * 1024.0));
    }
}

static void settings_refresh_device_resources(void)
{
    unsigned long long memory_total = 0;
    unsigned long long memory_available = 0;
    unsigned long long storage_total = 0;
    unsigned long long storage_available = 0;
#ifdef __linux__
    FILE * meminfo = fopen("/proc/meminfo", "r");
    if(meminfo != NULL) {
        char line[160];
        while(fgets(line, sizeof(line), meminfo) != NULL) {
            unsigned long long kib = 0;
            if(sscanf(line, "MemTotal: %llu kB", &kib) == 1)
                memory_total = kib * 1024ULL;
            else if(sscanf(line, "MemAvailable: %llu kB", &kib) == 1)
                memory_available = kib * 1024ULL;
        }
        fclose(meminfo);
    }
    struct statvfs filesystem;
    if(statvfs("/", &filesystem) == 0) {
        storage_total = (unsigned long long)filesystem.f_blocks * filesystem.f_frsize;
        storage_available = (unsigned long long)filesystem.f_bavail * filesystem.f_frsize;
    }
#elif defined(_WIN32)
    MEMORYSTATUSEX memory_status;
    memset(&memory_status, 0, sizeof(memory_status));
    memory_status.dwLength = sizeof(memory_status);
    if(GlobalMemoryStatusEx(&memory_status)) {
        memory_total = memory_status.ullTotalPhys;
        memory_available = memory_status.ullAvailPhys;
    }
    char current_path[MAX_PATH];
    if(GetCurrentDirectoryA(sizeof(current_path), current_path) != 0) {
        ULARGE_INTEGER available_bytes, total_bytes, free_bytes;
        if(GetDiskFreeSpaceExA(current_path, &available_bytes, &total_bytes, &free_bytes)) {
            storage_total = total_bytes.QuadPart;
            storage_available = available_bytes.QuadPart;
        }
    }
#endif
    char value[64];
    if(settings_memory_value != NULL) {
        if(memory_total != 0) {
            format_capacity(value, sizeof(value), memory_total, memory_available, true);
            lv_label_set_text(settings_memory_value, value);
        }
        else lv_label_set_text(settings_memory_value, "暂不可用");
    }
    if(settings_storage_value != NULL) {
        if(storage_total != 0) {
            format_capacity(value, sizeof(value), storage_total, storage_available, false);
            lv_label_set_text(settings_storage_value, value);
        }
        else lv_label_set_text(settings_storage_value, "暂不可用");
    }
}

static const lv_font_t * settings_font_for_size(uint8_t size)
{
    return hachimi_ui_font(size);
}

static lv_obj_t * create_settings_label(lv_obj_t * parent, const char * text,
                                        lv_coord_t x, lv_coord_t y,
                                        lv_coord_t width, lv_coord_t height,
                                        lv_color_t color, uint8_t font_size)
{
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, settings_font_for_size(font_size), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

static lv_obj_t * create_settings_card(lv_obj_t * parent, lv_coord_t x, lv_coord_t y,
                                       lv_coord_t width, lv_coord_t row_height,
                                       uint32_t row_count)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, row_height * (lv_coord_t)row_count);
    lv_obj_set_style_bg_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 17, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    for(uint32_t i = 1; i < row_count; i++) {
        lv_obj_t * divider = lv_obj_create(card);
        lv_obj_remove_style_all(divider);
        lv_obj_set_pos(divider, 42, (lv_coord_t)(i * row_height));
        lv_obj_set_size(divider, width - 56, 1);
        lv_obj_set_style_bg_color(divider, lv_color_hex(0xeeeeF2), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    return card;
}

static lv_obj_t * create_settings_badge(lv_obj_t * parent, const char * icon,
                                       lv_color_t color, lv_coord_t x, lv_coord_t y,
                                       lv_coord_t size)
{
    lv_obj_t * badge = lv_obj_create(parent);
    lv_obj_remove_style_all(badge);
    lv_obj_set_pos(badge, x, y);
    lv_obj_set_size(badge, size, size);
    lv_obj_set_style_radius(badge, size / 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * glyph = lv_label_create(badge);
    lv_label_set_text(glyph, icon);
    lv_obj_set_style_text_font(glyph, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(glyph, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(glyph);
    lv_obj_clear_flag(glyph, LV_OBJ_FLAG_CLICKABLE);
    return glyph;
}

static lv_obj_t * create_settings_image_icon(lv_obj_t * parent, const char * source,
                                             lv_coord_t x, lv_coord_t y)
{
    lv_obj_t * icon = lv_image_create(parent);
    lv_image_set_src(icon, source);
    lv_obj_set_pos(icon, x, y);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return icon;
}

static lv_obj_t * create_settings_info_row(lv_obj_t * card, uint32_t row,
                                           lv_coord_t row_height, lv_coord_t card_width,
                                           const char * icon, const char * title, const char * value,
                                           bool show_chevron)
{
    lv_coord_t y = (lv_coord_t)(row * row_height);
    lv_coord_t text_y = y + (row_height - 22) / 2;
    create_settings_badge(card, icon, lv_color_hex(0x96979d), 13,
                          y + (row_height - 20) / 2, 20);
    create_settings_label(card, title, 42, text_y, 200, 22,
                          lv_color_hex(0x202124), 14);
    lv_obj_t * value_label = create_settings_label(card, value,
                                                    card_width - (show_chevron ? 178 : 296),
                                                    text_y, show_chevron ? 140 : 280, 22,
                                                    lv_color_hex(0x707078), 14);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    if(show_chevron) {
        lv_obj_t * chevron = lv_label_create(card);
        lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
        lv_obj_set_pos(chevron, card_width - 28, y + (row_height - 14) / 2);
        lv_obj_set_size(chevron, 14, 18);
        lv_obj_set_style_text_font(chevron, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(chevron, lv_color_hex(0xb6b6bb), LV_PART_MAIN);
        lv_obj_clear_flag(chevron, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    return value_label;
}

static void settings_nav_event_cb(lv_event_t * event)
{
    const settings_page_t * page = lv_event_get_user_data(event);
    if(page != NULL) settings_show_page(*page);
}

static void settings_show_page(settings_page_t page)
{
    if(page < SETTINGS_PAGE_ABOUT || page >= SETTINGS_PAGE_COUNT) return;
    if(settings_keyboard != NULL) {
        lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(settings_keyboard, NULL);
    }
    for(uint32_t i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        if(i == (uint32_t)page) {
            lv_obj_clear_flag(settings_pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(settings_nav_buttons[i], lv_color_hex(0xcbd1db), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(settings_nav_buttons[i], LV_OPA_COVER, LV_PART_MAIN);
        }
        else {
            lv_obj_add_flag(settings_pages[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(settings_nav_buttons[i], LV_OPA_TRANSP, LV_PART_MAIN);
        }
    }
    if(page == SETTINGS_PAGE_NETWORK) ios_network_refresh();
    if(page == SETTINGS_PAGE_DISPLAY) ios_display_refresh();
    if(page == SETTINGS_PAGE_TIME) ios_time_refresh();
    if(page == SETTINGS_PAGE_ABOUT) settings_refresh_device_resources();
}

static lv_obj_t * create_settings_nav_button(lv_obj_t * parent, settings_page_t page,
                                             const char * icon_asset, const char * title)
{
    lv_obj_t * button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_pos(button, 10, 184 + (lv_coord_t)page * 42);
    lv_obj_set_size(button, 198, 36);
    lv_obj_set_style_radius(button, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, settings_nav_event_cb, LV_EVENT_CLICKED,
                        (void *)&settings_page_ids[page]);
    if(page == SETTINGS_PAGE_DISPLAY) {
        settings_nav_icons[page] = create_settings_image_icon(
            button, IOS_ASSET("settings_display_nav.png"), 10, 7);
    }
    else {
        settings_nav_icons[page] = create_settings_image_icon(button, icon_asset, 10, 7);
    }
    settings_nav_labels[page] = create_settings_label(button, title, 42, 8, 146, 22,
                                                       lv_color_hex(0x202124), 14);
    settings_nav_buttons[page] = button;
    return button;
}

static void settings_search_event_cb(lv_event_t * event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(settings_keyboard, settings_search);
        lv_obj_clear_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_keyboard);
    }
    else if(code == LV_EVENT_VALUE_CHANGED) {
        const char * query = lv_textarea_get_text(settings_search);
        static const char * terms[] = {
            "关于设备 about device Hachimi",
            "网络与连接 network wifi Wi-Fi bluetooth",
            "显示与亮度 display brightness backlight",
            "时间与日期 time date clock timezone 时区 校时",
        };
        for(uint32_t i = 0; i < SETTINGS_PAGE_COUNT; i++) {
            if(query[0] == '\0' || strstr(terms[i], query) != NULL)
                lv_obj_clear_flag(settings_nav_buttons[i], LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(settings_nav_buttons[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void settings_keyboard_event_cb(lv_event_t * event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(settings_keyboard, NULL);
        lv_obj_remove_state(settings_search, LV_STATE_FOCUSED);
    }
}

static void create_settings_intro(lv_obj_t * page, bool network)
{
    lv_obj_t * card = create_settings_card(page, 14, 50, 554, 112, 1);
    if(network) {
        create_settings_image_icon(card, IOS_ASSET("settings_network.png"), 14, 12);
    }
    else {
        create_settings_image_icon(card, IOS_ASSET("settings_about.png"), 14, 12);
    }
    create_settings_label(card, network ? "网络与连接" : "关于设备", 14, 56, 500, 30,
                          lv_color_hex(0x17181b), 22);
    create_settings_label(card, network ? "管理 Wi-Fi、蓝牙、以太网与个人热点。" :
                                         "查看设备信息、显示规格与软件版本。",
                          14, 88, 520, 20, lv_color_hex(0x85858b), 12);
}

static lv_obj_t * create_settings_app(lv_obj_t * parent)
{

    settings_root = lv_obj_create(parent);
    lv_obj_remove_style_all(settings_root);
    lv_obj_set_pos(settings_root, 0, 0);
    lv_obj_set_size(settings_root, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(settings_root, lv_color_hex(0xf2f2f7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(settings_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(settings_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * sidebar = lv_obj_create(settings_root);
    lv_obj_remove_style_all(sidebar);
    lv_obj_set_pos(sidebar, 0, 0);
    lv_obj_set_size(sidebar, 218, DISPLAY_HEIGHT);
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
    settings_search = lv_textarea_create(search_box);
    lv_obj_remove_style_all(settings_search);
    lv_obj_set_pos(settings_search, 31, 4);
    lv_obj_set_size(settings_search, 153, 23);
    lv_obj_set_style_text_font(settings_search, settings_font_for_size(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(settings_search, lv_color_hex(0x30333a), LV_PART_MAIN);
    lv_obj_set_style_text_color(settings_search, lv_color_hex(0x818893), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(settings_search, lv_color_hex(0x30333a), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(settings_search, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(settings_search, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_textarea_set_one_line(settings_search, true);
    lv_obj_set_height(settings_search, 23);
    lv_obj_add_flag(settings_search, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_textarea_set_max_length(settings_search, 32);
    lv_textarea_set_placeholder_text(settings_search, "搜索");
    lv_textarea_set_text(settings_search, "");
    lv_obj_set_scrollbar_mode(settings_search, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * device_icon = lv_image_create(sidebar);
    lv_image_set_src(device_icon, IOS_ASSET("settings.png"));
    lv_image_set_pivot(device_icon, 0, 0);
    lv_image_set_scale(device_icon, 144);
    lv_image_set_inner_align(device_icon, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_set_pos(device_icon, 18, 108);
    lv_obj_clear_flag(device_icon, LV_OBJ_FLAG_CLICKABLE);
    create_settings_label(sidebar, "设置", 65, 122, 140, 24,
                          lv_color_hex(0x17181b), 16);
    create_settings_nav_button(sidebar, SETTINGS_PAGE_ABOUT,
                               IOS_ASSET("settings_about_nav.png"), "关于设备");
    create_settings_nav_button(sidebar, SETTINGS_PAGE_NETWORK,
                               IOS_ASSET("settings_network_nav.png"), "网络与连接");
    create_settings_nav_button(sidebar, SETTINGS_PAGE_DISPLAY,
                               IOS_ASSET("settings_display_nav.png"), "显示与亮度");
    create_settings_nav_button(sidebar, SETTINGS_PAGE_TIME,
                               IOS_ASSET("settings_time_nav.png"), "时间与日期");

    for(uint32_t i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        settings_pages[i] = lv_obj_create(settings_root);
        lv_obj_remove_style_all(settings_pages[i]);
        lv_obj_set_pos(settings_pages[i], 218, IOS_APP_CONTENT_PANEL_TOP);
        lv_obj_set_size(settings_pages[i], 582, DISPLAY_HEIGHT - IOS_APP_CONTENT_PANEL_TOP);
        lv_obj_set_style_bg_color(settings_pages[i], lv_color_hex(0xf2f2f7), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(settings_pages[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(settings_pages[i], LV_OBJ_FLAG_SCROLLABLE);
        if(i == SETTINGS_PAGE_ABOUT) create_settings_intro(settings_pages[i], false);
    }
    lv_obj_add_flag(settings_pages[SETTINGS_PAGE_ABOUT], LV_OBJ_FLAG_SCROLLABLE |
                    LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(settings_pages[SETTINGS_PAGE_ABOUT], LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings_pages[SETTINGS_PAGE_ABOUT], LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t * about_card = create_settings_card(settings_pages[SETTINGS_PAGE_ABOUT], 14, 184, 554, 32, 4);
    create_settings_info_row(about_card, 0, 32, 554, LV_SYMBOL_HOME, "设备名称", "Hachimi", false);
    create_settings_info_row(about_card, 1, 32, 554, LV_SYMBOL_SETTINGS, "中央处理器", "NXP i.MX6ULL (ARM Cortex-A7)", false);
    create_settings_info_row(about_card, 2, 32, 554, LV_SYMBOL_DRIVE, "Linux 版本", "Debian 10 (buster) / 4.19.35-imx6", false);
    create_settings_info_row(about_card, 3, 32, 554, LV_SYMBOL_LIST, "序列号", "未提供", false);
    lv_obj_t * software_card = create_settings_card(settings_pages[SETTINGS_PAGE_ABOUT], 14, 326, 554, 32, 3);
    create_settings_info_row(software_card, 0, 32, 554, LV_SYMBOL_IMAGE, "显示分辨率", "800 x 480", false);
    create_settings_info_row(software_card, 1, 32, 554, LV_SYMBOL_REFRESH, "图形框架", "LVGL 9.4.0", false);
    create_settings_info_row(software_card, 2, 32, 554, LV_SYMBOL_DIRECTORY, "UI 版本", "Homo OS v0.1.0", false);
    lv_obj_t * resource_card = create_settings_card(settings_pages[SETTINGS_PAGE_ABOUT], 14, 434, 554, 32, 2);
    settings_memory_value = create_settings_info_row(resource_card, 0, 32, 554,
        LV_SYMBOL_SETTINGS, "内存", "读取中…", false);
    settings_storage_value = create_settings_info_row(resource_card, 1, 32, 554,
        LV_SYMBOL_DRIVE, "存储空间", "读取中…", false);
    lv_obj_set_style_text_align(settings_memory_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_align(settings_storage_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    ios_network_create(settings_pages[SETTINGS_PAGE_NETWORK], settings_font_for_size(14),
                       settings_font_for_size(12), settings_font_for_size(18));
    ios_display_create(settings_pages[SETTINGS_PAGE_DISPLAY], settings_font_for_size(14),
                       settings_font_for_size(12), settings_font_for_size(18),
                       settings_font_for_size(22));
    ios_time_create(settings_pages[SETTINGS_PAGE_TIME], settings_font_for_size(14),
                    settings_font_for_size(12), settings_font_for_size(18),
                    settings_font_for_size(22));
    ios_status_bar_create(settings_root, lv_color_hex(0x202124));
    settings_keyboard = lv_keyboard_create(settings_root);
    lv_obj_set_style_align(settings_keyboard, LV_ALIGN_TOP_LEFT, LV_PART_MAIN);
    lv_obj_set_pos(settings_keyboard, 40, 274);
    lv_obj_set_size(settings_keyboard, 720, 166);
    lv_obj_add_event_cb(settings_keyboard, settings_keyboard_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(settings_search, settings_search_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(search_box, settings_search_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_flag(settings_root, LV_OBJ_FLAG_HIDDEN);
    settings_show_page(SETTINGS_PAGE_ABOUT);
    return settings_root;
}

static void settings_open(lv_obj_t * view)
{
    (void)view;
    settings_show_page(SETTINGS_PAGE_ABOUT);
}

static void settings_close(lv_obj_t * view)
{
    (void)view;
    lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(settings_keyboard, NULL);
}

const hachimi_app_module_t hachimi_settings_app = {
    "settings", create_settings_app, settings_open, settings_close, NULL, NULL
};

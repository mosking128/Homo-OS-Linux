#include "mail_app.h"
#include "apps/demo/demo_ui.h"
#include "framework/desktop.h"
#include <string.h>

typedef struct {
    const char * sender, * initials, * subject, * preview, * date, * body;
    uint32_t color;
    bool unread, flagged;
} demo_mail_t;

static demo_mail_t messages[] = {
    {"林晓", "晓", "周末的湖边散步", "我发现了一家新开的咖啡店，环境很不错。", "09:32",
     "早上好！\n\n周末天气看起来不错，要不要一起去湖边走走？我还发现了一家新开的咖啡店，就在北海公园附近。\n\n我把地址和菜单放在下面了，到时候我们可以慢慢逛。\n\n期待周末见！", 0xed9870, true, true},
    {"Apple", "A", "每一天，发现新的可能。", "了解本周为你精选的内容与灵感。", "昨天",
     "你好，\n\n为你准备了一些本周精选内容。打开设备，继续探索你感兴趣的创作、照片和灵感。\n\n祝你今天愉快。", 0x26272b, true, false},
    {"周雨桐", "桐", "旅行照片已整理完成", "我把上周的照片做成了一个小相册。", "周二",
     "嗨！\n\n上周旅行的照片我整理好啦，挑了几张最喜欢的做成小相册。光线特别好的那几张我也单独放出来了。\n\n有空一起看看吧。", 0xb780d4, false, false},
    {"Hachimi 团队", "H", "欢迎使用 Hachimi", "这是一个用来展示邮件布局的示范邮件。", "周一",
     "欢迎来到 Hachimi。\n\n此邮件为示例内容，用于展示收件箱和阅读页面。\n\n祝使用愉快！", 0x448bc4, false, false},
};

static lv_obj_t * root, * list_panel, * detail_panel, * detail_body, * compose_panel;
static lv_obj_t * rows[sizeof(messages) / sizeof(messages[0])], * search_field, * keyboard;
static lv_obj_t * inbox_button, * flagged_button, * list_heading;
static unsigned selected_message;
static bool flagged_folder;

static void render_detail(void)
{
    if(!detail_body) return;
    lv_obj_clean(detail_body);
    demo_label(detail_body, messages[selected_message].subject, 24, 18, 430, 34, 24, DEMO_INK);
    demo_avatar(detail_body, messages[selected_message].initials, 25, 68, 42,
                messages[selected_message].color);
    demo_label(detail_body, messages[selected_message].sender, 80, 69, 190, 23, 15, DEMO_INK);
    demo_label(detail_body, "收件人：我  ·  详情", 80, 93, 260, 19, 12, DEMO_MUTED);
    demo_label(detail_body, messages[selected_message].date, 365, 75, 75, 18, 11, DEMO_MUTED);
    lv_obj_t * body = lv_label_create(detail_body);
    lv_label_set_text(body, messages[selected_message].body);
    lv_obj_set_pos(body, 25, 134);
    lv_obj_set_size(body, 420, 270);
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(body, hachimi_ui_font(15), 0);
    lv_obj_set_style_text_color(body, lv_color_hex(DEMO_INK), 0);
    lv_obj_set_style_text_line_space(body, 7, 0);
    demo_panel(detail_body, 25, 420, 420, 1, 0, DEMO_LINE);
    demo_label(detail_body, "回复", 25, 434, 90, 20, 12, DEMO_BLUE);
}

static void select_message(lv_event_t * event)
{
    if(keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    selected_message = (unsigned)(intptr_t)lv_event_get_user_data(event);
    if(selected_message >= sizeof(messages) / sizeof(messages[0])) selected_message = 0;
    messages[selected_message].unread = false;
    for(unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        lv_obj_set_style_bg_color(rows[i], lv_color_hex(i == selected_message ? 0xe9f2ff : 0xffffff), 0);
    }
    render_detail();
}

static void update_filter(lv_event_t * event)
{
    (void)event;
    const char * query = lv_textarea_get_text(search_field);
    for(unsigned i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
        bool match = (!flagged_folder || messages[i].flagged) &&
                     (!query || !*query || strstr(messages[i].sender, query) ||
                      strstr(messages[i].subject, query) || strstr(messages[i].preview, query));
        if(match) lv_obj_clear_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    if(list_heading) lv_label_set_text(list_heading, flagged_folder ? "已加旗标" : "收件箱");
}

static void switch_folder(lv_event_t * event)
{
    flagged_folder = (intptr_t)lv_event_get_user_data(event) != 0;
    lv_obj_set_style_bg_color(inbox_button, lv_color_hex(flagged_folder ? 0xffffff : 0xe5efff), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(inbox_button, 0), lv_color_hex(flagged_folder ? DEMO_MUTED : DEMO_BLUE), 0);
    lv_obj_set_style_bg_color(flagged_button, lv_color_hex(flagged_folder ? 0xe5efff : 0xffffff), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(flagged_button, 0), lv_color_hex(flagged_folder ? DEMO_BLUE : DEMO_MUTED), 0);
    update_filter(NULL);
}

static void return_to_mail(lv_event_t * event)
{
    (void)event;
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(list_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(detail_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(compose_panel, LV_OBJ_FLAG_HIDDEN);
}

static void open_compose(lv_event_t * event)
{
    (void)event;
    lv_obj_add_flag(list_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(detail_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(compose_panel, LV_OBJ_FLAG_HIDDEN);
}

static void focus_composer(lv_event_t * event)
{
    lv_obj_t * field = lv_event_get_target(event);
    if(lv_event_get_code(event) == LV_EVENT_FOCUSED || lv_event_get_code(event) == LV_EVENT_PRESSED) {
        lv_keyboard_set_textarea(keyboard, field);
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void toggle_flag(lv_event_t * event)
{
    (void)event;
    messages[selected_message].flagged = !messages[selected_message].flagged;
    update_filter(NULL);
}

static lv_obj_t * make_field(lv_obj_t * parent, const char * placeholder,
                             int x, int y, int w, int h)
{
    lv_obj_t * field = lv_textarea_create(parent);
    lv_obj_set_pos(field, x, y);
    lv_obj_set_size(field, w, h);
    lv_textarea_set_one_line(field, h < 50);
    lv_textarea_set_placeholder_text(field, placeholder);
    lv_obj_set_style_radius(field, 10, 0);
    lv_obj_set_style_bg_color(field, lv_color_hex(0xf5f5f7), 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(field, 0, 0);
    lv_obj_set_style_text_font(field, hachimi_ui_font(14), 0);
    lv_obj_add_event_cb(field, focus_composer, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(field, focus_composer, LV_EVENT_PRESSED, NULL);
    return field;
}

static lv_obj_t * create(lv_obj_t * parent)
{
    root = demo_panel(parent, 0, 0, 800, 480, 0, 0xffffff);
    list_panel = demo_panel(root, 0, 0, 320, 480, 0, 0xf7f7f9);
    demo_label(list_panel, "信箱", 18, 15, 210, 39, 28, DEMO_INK);
    demo_button(list_panel, "+", 268, 15, 38, 38, 23, DEMO_BLUE, 0xffffff, open_compose, 0);
    lv_obj_t * search_bg = demo_panel(list_panel, 14, 61, 292, 34, 10, 0xffffff);
    search_field = lv_textarea_create(search_bg);
    lv_obj_remove_style_all(search_field);
    lv_obj_set_pos(search_field, 11, 5);
    lv_obj_set_size(search_field, 270, 24);
    lv_textarea_set_one_line(search_field, true);
    lv_textarea_set_placeholder_text(search_field, "搜索邮件");
    lv_obj_set_style_text_font(search_field, hachimi_ui_font(14), 0);
    lv_obj_add_event_cb(search_field, update_filter, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(search_field, focus_composer, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(search_field, focus_composer, LV_EVENT_PRESSED, NULL);
    inbox_button = demo_button(list_panel, "收件箱", 14, 105, 141, 32, 13, DEMO_BLUE, 0xe5efff, switch_folder, 0);
    flagged_button = demo_button(list_panel, "已加旗标", 163, 105, 143, 32, 13, DEMO_MUTED, 0xffffff, switch_folder, 1);
    list_heading = demo_label(list_panel, "收件箱", 18, 145, 275, 19, 12, DEMO_MUTED);
    for(unsigned i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
        int y = 168 + (int)i * 76;
        rows[i] = demo_button(list_panel, "", 7, y, 306, 72, 13, DEMO_INK,
                              i == selected_message ? 0xe9f2ff : 0xffffff,
                              select_message, (intptr_t)i);
        if(messages[i].unread) demo_panel(rows[i], 9, 29, 7, 7, 4, DEMO_BLUE);
        demo_label(rows[i], messages[i].sender, 25, 8, 174, 20, 14, DEMO_INK);
        demo_label(rows[i], messages[i].date, 232, 8, 60, 18, 11, DEMO_MUTED);
        demo_label(rows[i], messages[i].subject, 25, 30, 268, 19, 13, DEMO_INK);
        demo_label(rows[i], messages[i].preview, 25, 51, 268, 17, 11, DEMO_MUTED);
        if(i + 1 < sizeof(messages) / sizeof(messages[0])) demo_rule(rows[i], 25, 71, 270);
    }
    demo_panel(root, 319, 0, 1, 480, 0, DEMO_LINE);
    detail_panel = demo_panel(root, 320, 0, 480, 480, 0, 0xffffff);
    demo_button(detail_panel, LV_SYMBOL_LEFT, 13, 12, 42, 38, 16, DEMO_BLUE, 0xf4f5f7, NULL, 0);
    demo_button(detail_panel, LV_SYMBOL_REFRESH, 349, 12, 42, 38, 15, DEMO_BLUE, 0xf4f5f7, NULL, 0);
    demo_button(detail_panel, "旗标", 397, 12, 50, 38, 12, DEMO_BLUE, 0xf4f5f7, toggle_flag, 0);
    demo_rule(detail_panel, 0, 61, 480);
    detail_body = demo_panel(detail_panel, 0, 62, 480, 418, 0, 0xffffff);
    render_detail();

    compose_panel = demo_panel(root, 0, 0, 800, 480, 0, 0xffffff);
    demo_button(compose_panel, "取消", 16, 14, 70, 34, 14, DEMO_BLUE, 0xf4f5f7, return_to_mail, 0);
    demo_label(compose_panel, "新邮件", 315, 18, 170, 27, 18, DEMO_INK);
    demo_label(compose_panel, "存为草稿", 704, 20, 82, 22, 13, DEMO_BLUE);
    demo_rule(compose_panel, 0, 60, 800);
    demo_label(compose_panel, "收件人", 22, 78, 75, 22, 14, DEMO_MUTED);
    make_field(compose_panel, "", 103, 70, 670, 38);
    demo_rule(compose_panel, 22, 112, 756);
    demo_label(compose_panel, "抄送/密送", 22, 128, 83, 22, 13, DEMO_MUTED);
    make_field(compose_panel, "", 112, 120, 660, 38);
    demo_rule(compose_panel, 22, 162, 756);
    demo_label(compose_panel, "主题", 22, 178, 75, 22, 14, DEMO_MUTED);
    make_field(compose_panel, "", 103, 170, 670, 38);
    demo_rule(compose_panel, 22, 212, 756);
    make_field(compose_panel, "编写邮件…", 22, 228, 756, 178);
    demo_label(compose_panel, "邮件示范页面", 22, 420, 260, 20, 12, DEMO_MUTED);
    keyboard = lv_keyboard_create(root);
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 382, 253);
    lv_obj_set_size(keyboard, 402, 215);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(compose_panel, LV_OBJ_FLAG_HIDDEN);
    return root;
}

const hachimi_app_module_t hachimi_mail_app = {"mail", create, NULL, NULL, NULL, NULL};

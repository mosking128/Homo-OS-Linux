#include "messages_app.h"
#include "apps/demo/demo_ui.h"
#include "framework/desktop.h"
#include <ctype.h>
#include <string.h>

typedef struct {
    const char * name, * initials, * preview, * time;
    uint32_t color;
    const char * thread[4], * thread_time;
} conversation_t;
static const conversation_t conversations[] = {
    {"林晓", "晓", "周末去湖边走走吗？", "09:41", 0xf09b65,
     {"嗨，最近怎么样？", "挺好的！这周末天气不错，想去湖边走走，顺便喝杯咖啡。",
      "好呀，我知道一家新开的店。", "听起来不错，发我地址吧。"}, "今天 09:41"},
    {"家庭", "家", "妈妈：到家记得说一声", "昨天", 0x6cbd79,
     {"妈妈：到家记得说一声", "我已经到家啦，晚饭后给你打电话。",
      "好的，路上注意安全，早点休息。", "知道啦，晚安。"}, "昨天 21:18"},
    {"陈一帆", "帆", "那家咖啡店我收藏好了", "周二", 0x7e8ee8,
     {"那家咖啡店我收藏好了", "太好了，周末去试试，听说他们的手冲不错。",
      "我也看了菜单，甜点看起来很棒。", "那就约在下午三点吧。"}, "周二 18:26"},
    {"周雨桐", "桐", "照片拍得真好看", "周一", 0xc47ad4,
     {"照片拍得真好看", "谢谢！湖边日落时光线特别好。",
      "可以把原图发我几张吗？我想做相册。", "没问题，等会儿发给你。"}, "周一 20:15"},
    {"产品讨论组", "产", "许安：我把新稿发群里了", "周日", 0x55a9b4,
     {"许安：我把新稿发群里了", "收到，我先看一遍，晚点整理反馈。",
      "李明：重点看一下首页和流程图。", "好的，交互细节我也会一起确认。"}, "周日 16:30"},
};
static lv_obj_t * root, * chat_panel, * chat_area, * compose_panel, * search_field, * keyboard;
static lv_obj_t * rows[sizeof(conversations) / sizeof(conversations[0])];
static unsigned selected_conversation;

static void chat_bubble(lv_obj_t * parent, int x, int y, int w, int h,
                        const char * text, bool outgoing)
{
    lv_obj_t * bubble = demo_panel(parent, x, y, w, h, 18,
                                   outgoing ? DEMO_BLUE : 0xf0f0f2);
    lv_obj_t * label = lv_label_create(bubble);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, 12, 8);
    lv_obj_set_size(label, w - 24, h - 16);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(label, hachimi_ui_font(13), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(outgoing ? 0xffffff : DEMO_INK), 0);
}

static void render_chat(void)
{
    if(!chat_area) return;
    lv_obj_clean(chat_area);
    const conversation_t * person = &conversations[selected_conversation];
    demo_avatar(chat_area, person->initials, 218, 7, 42, person->color);
    demo_label(chat_area, person->name, 12, 58, 476, 24, 16, DEMO_INK);
    demo_label(chat_area, "iMessage · 信息为示范内容", 12, 82, 476, 18, 12, DEMO_MUTED);
    chat_bubble(chat_area, 10, 112, 260, 56, person->thread[0], false);
    chat_bubble(chat_area, 180, 178, 300, 68, person->thread[1], true);
    chat_bubble(chat_area, 10, 256, 260, 58, person->thread[2], false);
    chat_bubble(chat_area, 210, 324, 270, 58, person->thread[3], true);
    demo_label(chat_area, person->thread_time, 206, 388, 280, 18, 11, DEMO_MUTED);
    demo_rule(chat_panel, 1, 410, 498);
}

static void choose_conversation(lv_event_t * event)
{
    selected_conversation = (unsigned)(intptr_t)lv_event_get_user_data(event);
    if(selected_conversation >= sizeof(conversations) / sizeof(conversations[0])) selected_conversation = 0;
    if(keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    if(compose_panel) lv_obj_add_flag(compose_panel, LV_OBJ_FLAG_HIDDEN);
    if(chat_panel) lv_obj_clear_flag(chat_panel, LV_OBJ_FLAG_HIDDEN);
    for(unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        lv_obj_set_style_bg_color(rows[i], lv_color_hex(i == selected_conversation ? 0xe7f1ff : DEMO_BG), 0);
    }
    render_chat();
}

static void filter_conversations(lv_event_t * event)
{
    (void)event;
    const char * query = lv_textarea_get_text(search_field);
    for(unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        bool match = !query || !*query || strstr(conversations[i].name, query) ||
                     strstr(conversations[i].preview, query);
        if(match) lv_obj_clear_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void back_to_chat(lv_event_t * event)
{
    (void)event;
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(chat_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(compose_panel, LV_OBJ_FLAG_HIDDEN);
}

static void open_compose(lv_event_t * event)
{
    (void)event;
    lv_obj_add_flag(chat_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(compose_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void focus_composer(lv_event_t * event)
{
    lv_obj_t * field = lv_event_get_target(event);
    if(lv_event_get_code(event) == LV_EVENT_FOCUSED || lv_event_get_code(event) == LV_EVENT_PRESSED) {
        lv_keyboard_set_textarea(keyboard, field);
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t * make_field(lv_obj_t * parent, const char * placeholder,
                             int x, int y, int w, int h)
{
    lv_obj_t * field = lv_textarea_create(parent);
    lv_obj_set_pos(field, x, y);
    lv_obj_set_size(field, w, h);
    lv_textarea_set_one_line(field, h < 50);
    lv_textarea_set_placeholder_text(field, placeholder);
    lv_textarea_set_max_length(field, 120);
    lv_obj_set_style_radius(field, 12, 0);
    lv_obj_set_style_bg_color(field, lv_color_hex(0xf3f4f6), 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(field, 0, 0);
    lv_obj_set_style_text_font(field, hachimi_ui_font(14), 0);
    lv_obj_set_style_text_color(field, lv_color_hex(DEMO_INK), 0);
    lv_obj_add_event_cb(field, focus_composer, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(field, focus_composer, LV_EVENT_PRESSED, NULL);
    return field;
}

static lv_obj_t * create(lv_obj_t * parent)
{
    root = demo_panel(parent, 0, 0, 800, 480, 0, 0xffffff);
    lv_obj_t * sidebar = demo_panel(root, 0, 0, 300, 480, 0, DEMO_BG);
    demo_label(sidebar, "信息", 18, 17, 180, 38, 28, DEMO_INK);
    demo_button(sidebar, LV_SYMBOL_EDIT, 247, 16, 39, 39, 18, DEMO_BLUE, 0xffffff, open_compose, 0);
    lv_obj_t * search_bg = demo_panel(sidebar, 14, 65, 272, 34, 10, 0xffffff);
    search_field = lv_textarea_create(search_bg);
    lv_obj_remove_style_all(search_field);
    lv_obj_set_pos(search_field, 12, 5);
    lv_obj_set_size(search_field, 248, 24);
    lv_textarea_set_one_line(search_field, true);
    lv_textarea_set_placeholder_text(search_field, "搜索");
    lv_obj_set_style_text_font(search_field, hachimi_ui_font(14), 0);
    lv_obj_set_style_text_color(search_field, lv_color_hex(DEMO_INK), 0);
    lv_obj_add_event_cb(search_field, filter_conversations, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(search_field, focus_composer, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(search_field, focus_composer, LV_EVENT_PRESSED, NULL);
    lv_obj_add_flag(search_field, LV_OBJ_FLAG_CLICKABLE);
    demo_label(sidebar, "信息", 16, 108, 260, 20, 12, DEMO_MUTED);
    for(unsigned i = 0; i < sizeof(conversations) / sizeof(conversations[0]); i++) {
        int y = 133 + (int)i * 68;
        rows[i] = demo_button(sidebar, "", 7, y, 286, 64, 14, DEMO_INK,
                              i == selected_conversation ? 0xe7f1ff : DEMO_BG,
                              choose_conversation, (intptr_t)i);
        demo_avatar(rows[i], conversations[i].initials, 10, 10, 44, conversations[i].color);
        demo_label(rows[i], conversations[i].name, 64, 9, 144, 22, 15, DEMO_INK);
        demo_label(rows[i], conversations[i].time, 218, 9, 54, 18, 11, DEMO_MUTED);
        demo_label(rows[i], conversations[i].preview, 64, 33, 210, 19, 12, DEMO_MUTED);
        if(i + 1 < sizeof(conversations) / sizeof(conversations[0])) demo_rule(rows[i], 64, 63, 218);
        demo_enable_event_bubble(rows[i]);
    }
    demo_panel(root, 299, 0, 1, 480, 0, DEMO_LINE);

    chat_panel = demo_panel(root, 300, 0, 500, 480, 0, 0xffffff);
    chat_area = demo_panel(chat_panel, 0, 0, 500, 410, 0, 0xffffff);
    render_chat();
    demo_button(chat_panel, LV_SYMBOL_LEFT, 12, 11, 40, 38, 17, DEMO_BLUE, 0xf3f4f6, NULL, 0);
    demo_button(chat_panel, LV_SYMBOL_VIDEO, 438, 11, 42, 38, 17, DEMO_BLUE, 0xf3f4f6, NULL, 0);
    demo_button(chat_panel, "+", 12, 422, 38, 38, 22, DEMO_BLUE, 0xf0f0f2, NULL, 0);
    lv_obj_t * composer = demo_panel(chat_panel, 59, 422, 372, 38, 18, 0xffffff);
    lv_obj_set_style_border_width(composer, 1, 0);
    lv_obj_set_style_border_color(composer, lv_color_hex(0xd7d9dd), 0);
    demo_label(composer, "iMessage", 13, 9, 285, 21, 13, DEMO_MUTED);
    demo_button(chat_panel, LV_SYMBOL_UP, 441, 422, 38, 38, 15, 0xffffff, DEMO_BLUE, NULL, 0);
    demo_label(chat_panel, "示范对话", 205, 463, 100, 14, 10, DEMO_MUTED);

    compose_panel = demo_panel(root, 300, 0, 500, 480, 0, 0xffffff);
    demo_button(compose_panel, "取消", 14, 14, 70, 34, 14, DEMO_BLUE, 0xf4f5f7, back_to_chat, 0);
    demo_label(compose_panel, "新信息", 174, 17, 150, 28, 18, DEMO_INK);
    demo_rule(compose_panel, 0, 60, 500);
    demo_label(compose_panel, "收件人", 17, 81, 75, 22, 14, DEMO_MUTED);
    make_field(compose_panel, "姓名", 92, 73, 390, 38);
    demo_rule(compose_panel, 14, 120, 472);
    demo_label(compose_panel, "消息", 17, 140, 75, 22, 14, DEMO_MUTED);
    make_field(compose_panel, "输入示例内容", 92, 132, 390, 70);
    demo_label(compose_panel, "此页面仅用于界面展示", 122, 216, 300, 22, 12, DEMO_MUTED);
    keyboard = lv_keyboard_create(root);
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 380, 253);
    lv_obj_set_size(keyboard, 404, 215);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(compose_panel, LV_OBJ_FLAG_HIDDEN);
    return root;
}

const hachimi_app_module_t hachimi_messages_app = {"messages", create, NULL, NULL, NULL, NULL};

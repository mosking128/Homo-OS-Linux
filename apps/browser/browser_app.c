#include "browser_app.h"
#include "apps/demo/demo_ui.h"

static lv_obj_t * create(lv_obj_t * parent)
{
    lv_obj_t * root = demo_panel(parent, 0, 0, 800, 480, 0, 0xf2f3f7);
    lv_obj_t * toolbar = demo_panel(root, 0, 0, 800, 60, 0, 0xffffff);

    demo_button(toolbar, LV_SYMBOL_LEFT, 12, 10, 40, 40, 17,
                DEMO_BLUE, 0xf4f5f7, NULL, 0);
    demo_button(toolbar, LV_SYMBOL_RIGHT, 58, 10, 40, 40, 17,
                DEMO_MUTED, 0xf4f5f7, NULL, 0);

    lv_obj_t * address = demo_panel(toolbar, 110, 8, 555, 44, 22, 0xf2f3f6);
    demo_label(address, "https://demo.hachimi.local", 18, 11, 470, 22, 14, DEMO_INK);
    demo_button(toolbar, LV_SYMBOL_REFRESH, 674, 10, 40, 40, 16,
                DEMO_BLUE, 0xf4f5f7, NULL, 0);
    demo_button(toolbar, "标签页", 722, 10, 66, 40, 13,
                DEMO_BLUE, 0xf4f5f7, NULL, 0);
    demo_rule(root, 0, 59, 800);

    lv_obj_t * page = demo_panel(root, 20, 74, 760, 390, 18, 0xffffff);
    demo_label(page, "浏览器示范页", 38, 24, 360, 34, 24, DEMO_INK);
    demo_label(page, "这是一个本地界面模板，展示地址栏、常用网站和网页卡片。",
               40, 62, 680, 24, 13, DEMO_MUTED);

    lv_obj_t * search = demo_panel(page, 38, 104, 684, 48, 24, 0xf4f5f7);
    demo_label(search, "搜索网页或输入网址", 20, 13, 640, 22, 14, DEMO_MUTED);

    demo_label(page, "常用网站", 40, 176, 200, 22, 15, DEMO_INK);
    static const char * names[] = {"Apple", "地图", "邮件", "照片", "新闻"};
    static const char * initials[] = {"A", "地", "邮", "照", "新"};
    static const uint32_t colors[] = {0x303136, 0x45a878, 0x4387df, 0xc47ad4, 0xf09b65};
    for(unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        int x = 108 + (int)i * 132;
        demo_avatar(page, initials[i], x, 211, 52, colors[i]);
        demo_label(page, names[i], x - 17, 272, 86, 20, 12, DEMO_INK);
    }

    demo_label(page, "推荐内容", 40, 312, 180, 22, 15, DEMO_INK);
    demo_rule(page, 38, 344, 684);
    demo_label(page, "发现更多精彩内容", 40, 355, 350, 23, 14, DEMO_INK);
    demo_label(page, "示例网页卡片 · 浏览器功能仅作界面展示", 40, 383, 520, 18, 11, DEMO_MUTED);

    return root;
}

const hachimi_app_module_t hachimi_browser_app = {"browser", create, NULL, NULL, NULL, NULL};

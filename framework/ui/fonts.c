#include "fonts.h"
#include "libs/tiny_ttf/lv_tiny_ttf.h"
#include <stddef.h>

/* Cached for the lifetime of the desktop; every app uses the same typefaces. */
const lv_font_t * hachimi_ui_font(unsigned size)
{
    static const unsigned sizes[] = {12, 14, 16, 18, 22, 28, 32, 42, 56, 64};
    static const lv_font_t * fonts[sizeof(sizes) / sizeof(sizes[0])];
    size_t index = 0;
    for(size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        if(sizes[i] <= size) index = i;
    }
    if(fonts[index] == NULL) {
        fonts[index] = lv_tiny_ttf_create_file("A:assets/ios/PingFang.ttf", sizes[index]);
        if(fonts[index] == NULL) fonts[index] = &lv_font_source_han_sans_sc_14_cjk;
    }
    return fonts[index];
}

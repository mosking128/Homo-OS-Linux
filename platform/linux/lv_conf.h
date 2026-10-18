/* Linux board configuration: share Hachimi's 800x480/RGB565 base config. */
#include "../../config/lv_conf_common.h"

#undef LV_MEM_SIZE
/* PNG cache, decode scratch, app snapshots and multiple retained app views
 * coexist. Keep headroom for a cold full-screen wallpaper decode on return. */
#define LV_MEM_SIZE (16U * 1024U * 1024U)

#undef LV_DEF_REFR_PERIOD
#define LV_DEF_REFR_PERIOD 16

/* The i.MX6ULL Cortex-A7 has NEON; use LVGL's vectorized software blending. */
#if defined(HACHIMI_USE_NEON) && HACHIMI_USE_NEON
#undef LV_USE_DRAW_SW_ASM
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NEON
#endif

#undef LV_FONT_MONTSERRAT_20
#define LV_FONT_MONTSERRAT_20 1

#undef LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
#define LV_FONT_SOURCE_HAN_SANS_SC_14_CJK 1

#undef LV_USE_FS_STDIO
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'A'
#define LV_FS_STDIO_PATH ""
#define LV_FS_STDIO_CACHE_SIZE 0

#undef LV_USE_LODEPNG
#define LV_USE_LODEPNG 1

#undef LV_USE_SDL
#define LV_USE_SDL 0

#undef LV_USE_LINUX_FBDEV
#define LV_USE_LINUX_FBDEV 1
#define LV_LINUX_FBDEV_BSD 0
/* Keep a full-screen buffer, but redraw only changed areas for smoother transitions. */
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_LINUX_FBDEV_BUFFER_COUNT 0
/* Only used by PARTIAL mode; FULL mode allocates the full display buffer. */
#define LV_LINUX_FBDEV_BUFFER_SIZE 60
#define LV_LINUX_FBDEV_MMAP 1

#undef LV_USE_EVDEV
#define LV_USE_EVDEV 1

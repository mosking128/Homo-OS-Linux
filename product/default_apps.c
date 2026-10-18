#include "default_apps.h"
#include <stdbool.h>
#include "apps/browser/browser_app.h"
#include "apps/music/music_app.h"
#include "apps/settings/settings_app.h"
#include "apps/files/ios_files.h"
#include "apps/books/books_app.h"
#include "apps/maps/maps_app.h"
#include "apps/messages/messages_app.h"
#include "apps/mail/mail_app.h"
#include "apps/weather/weather_app.h"
#include "apps/calculator/ios_calculator.h"
#include "apps/clock/ios_clock.h"
#include "apps/calendar/ios_calendar.h"
#include "apps/led_control/led_control_app.h"

#define IOS_ASSET(name) "A:assets/ios/" name

static const hachimi_app_descriptor_t home_apps[] = {
    {HACHIMI_APP_BROWSER, IOS_ASSET("browser.png"), IOS_ASSET("labels/browser.png"), IOS_ASSET("labels/title_browser.png"), &hachimi_browser_app, false},
    {HACHIMI_APP_BOOKS, IOS_ASSET("books.png"), IOS_ASSET("labels/books.png"), IOS_ASSET("labels/title_books.png"), &hachimi_books_app, false},
    {HACHIMI_APP_CALCULATOR, IOS_ASSET("calculator.png"), IOS_ASSET("labels/calculator.png"), IOS_ASSET("labels/title_calculator.png"), &hachimi_calculator_app, false},
    {HACHIMI_APP_CALENDAR, IOS_ASSET("calendar.png"), IOS_ASSET("labels/calendar.png"), IOS_ASSET("labels/title_calendar.png"), &hachimi_calendar_app, false},
    {HACHIMI_APP_CLOCK, IOS_ASSET("clock.png"), IOS_ASSET("labels/clock.png"), IOS_ASSET("labels/title_clock.png"), &hachimi_clock_app, false},
    {HACHIMI_APP_FILES, IOS_ASSET("files.png"), IOS_ASSET("labels/files.png"), IOS_ASSET("labels/title_files.png"), &hachimi_files_app, false},
    {HACHIMI_APP_LED_CONTROL, IOS_ASSET("led_control.png"), IOS_ASSET("labels/led_control.png"), IOS_ASSET("labels/title_led_control.png"), &hachimi_led_control_app, false},
    {HACHIMI_APP_MAPS, IOS_ASSET("map.png"), IOS_ASSET("labels/map.png"), IOS_ASSET("labels/title_map.png"), &hachimi_maps_app, true},
    {HACHIMI_APP_MESSAGES, IOS_ASSET("messages.png"), IOS_ASSET("labels/messages.png"), IOS_ASSET("labels/title_messages.png"), &hachimi_messages_app, false},
    {HACHIMI_APP_MUSIC, IOS_ASSET("music.png"), IOS_ASSET("labels/music.png"), IOS_ASSET("labels/title_music.png"), &hachimi_music_app, false},
    {HACHIMI_APP_SETTINGS, IOS_ASSET("settings.png"), IOS_ASSET("labels/settings.png"), IOS_ASSET("labels/title_settings.png"), &hachimi_settings_app, false},
    {HACHIMI_APP_WEATHER, IOS_ASSET("weather.png"), IOS_ASSET("labels/weather.png"), IOS_ASSET("labels/title_weather.png"), &hachimi_weather_app, false},
    {HACHIMI_APP_MAIL, IOS_ASSET("mail.png"), IOS_ASSET("labels/mail.png"), IOS_ASSET("labels/title_mail.png"), &hachimi_mail_app, false},
};

static const hachimi_app_descriptor_t dock_apps[] = {
    {HACHIMI_APP_SETTINGS, IOS_ASSET("settings.png"), NULL, IOS_ASSET("labels/title_settings.png"), &hachimi_settings_app, false},
    {HACHIMI_APP_BROWSER, IOS_ASSET("browser.png"), NULL, IOS_ASSET("labels/title_browser.png"), &hachimi_browser_app, false},
    {HACHIMI_APP_MUSIC, IOS_ASSET("music.png"), NULL, IOS_ASSET("labels/title_music.png"), &hachimi_music_app, false},
    {HACHIMI_APP_FILES, IOS_ASSET("files.png"), NULL, IOS_ASSET("labels/title_files.png"), &hachimi_files_app, false},
    {HACHIMI_APP_WEATHER, IOS_ASSET("weather.png"), NULL, IOS_ASSET("labels/title_weather.png"), &hachimi_weather_app, false},
};

static const hachimi_app_module_t * const background_modules[] = {
    &hachimi_clock_app, &hachimi_music_app, &hachimi_weather_app,
};

const hachimi_app_catalog_t hachimi_default_apps = {
    home_apps, sizeof(home_apps) / sizeof(home_apps[0]),
    dock_apps, sizeof(dock_apps) / sizeof(dock_apps[0]),
    background_modules, sizeof(background_modules) / sizeof(background_modules[0]),
};

/* Compatibility entry point. New products call hachimi_desktop_create directly. */
void ios_desktop_create(void)
{
    (void)hachimi_desktop_create(&hachimi_default_apps);
}

#include "services/filesystem.h"
#include "services/clock.h"
#include "services/display.h"
#include "services/network.h"
#include "services/time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { if(!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while(0)

int main(int argc, char ** argv)
{
    char root[] = "/tmp/hachimi-service-smoke-XXXXXX";
    CHECK(mkdtemp(root));
    char dir[1024], first[1024], renamed[1024], link[1024];
    CHECK(hachimi_fs_join(root, "folder", dir, sizeof(dir)));
    CHECK(hachimi_fs_mkdir(dir));
    CHECK(hachimi_fs_join(dir, "note.txt", first, sizeof(first)));
    CHECK(hachimi_fs_join(dir, "renamed.txt", renamed, sizeof(renamed)));
    CHECK(hachimi_fs_write_text(first, "hello 哈基米", true));
    CHECK(!hachimi_fs_write_text(first, "must not overwrite", true));
    CHECK(!hachimi_fs_write_text(renamed, "missing file", false));
    char contents[128]; size_t length;
    CHECK(hachimi_fs_read_text(first, contents, sizeof(contents), &length));
    CHECK(strcmp(contents, "hello 哈基米") == 0);
    CHECK(hachimi_fs_write_text(first, "updated", false));
    CHECK(hachimi_fs_rename(first, renamed));
    CHECK(!hachimi_fs_exists(first));
    CHECK(hachimi_fs_write_text(first, "preserved", true));
    CHECK(!hachimi_fs_rename(renamed, first));
    CHECK(hachimi_fs_join(dir, "outside-link", link, sizeof(link)));
    CHECK(symlink("/tmp", link) == 0);
    hachimi_fs_entry_t entries[8]; size_t count;
    CHECK(hachimi_fs_list(dir, entries, 8, &count) && count == 2);
    CHECK(!hachimi_fs_valid_name("../escape"));
    CHECK(!hachimi_fs_valid_name(".."));
    CHECK(hachimi_fs_remove(root));
    CHECK(!hachimi_fs_exists(root) && hachimi_fs_is_directory("/tmp"));
    puts("PASS filesystem: create/read/edit/rename/no-clobber/list/delete/symlink");

    char clock[16];
    CHECK(hachimi_clock_format(clock, sizeof(clock), "%H:%M"));
    CHECK(strlen(clock) == 5 && clock[2] == ':');
    puts("PASS clock: system local time");

    if(argc <= 1 || strcmp(argv[1], "--board")) return 0;
    CHECK(hachimi_system_services_supported());
    static hachimi_network_service_t network;
    hachimi_network_init(&network);
    CHECK(!hachimi_network_request(&network, "invalid-operation", NULL));
    CHECK(hachimi_network_request(&network, "status", NULL));
    for(int i = 0; i < 2000 && !hachimi_network_poll(&network); i++) usleep(10000);
    CHECK(!network.job.running && !network.failed);
    CHECK(network.state.wifi_present);
    printf("PASS network: Wi-Fi available, Ethernet %s\n", network.state.ethernet_ip);
    CHECK(hachimi_network_request(&network, "scan", NULL));
    for(int i = 0; i < 3000 && !hachimi_network_poll(&network); i++) usleep(10000);
    CHECK(!network.job.running && !network.failed);
    printf("PASS network scan: %zu access points\n", network.state.network_count);

    static hachimi_time_service_t time_service;
    hachimi_time_init(&time_service);
    CHECK(hachimi_time_request(&time_service, "status", NULL));
    for(int i = 0; i < 1500 && !hachimi_time_poll(&time_service); i++) usleep(10000);
    CHECK(!time_service.job.running && time_service.succeeded);
    CHECK(time_service.state.year >= 2026 && time_service.state.timezone[0]);
    CHECK(hachimi_time_request(&time_service, "zones", NULL));
    for(int i = 0; i < 1500 && !hachimi_time_poll(&time_service); i++) usleep(10000);
    CHECK(!time_service.job.running && time_service.succeeded);
    CHECK(strstr(time_service.zones, time_service.state.timezone));
    printf("PASS time: %s %s, NTP=%d synchronized=%d, zones loaded\n", time_service.state.clock,
           time_service.state.timezone, time_service.state.ntp_enabled, time_service.state.synchronized);

    int original, current;
    CHECK(hachimi_display_get_brightness(&original));
    bool saved = hachimi_display_set_brightness(55) && hachimi_display_set_brightness(67) &&
                 hachimi_display_restore_brightness() && hachimi_display_get_brightness(&current) &&
                 current == 67;
    bool safe = hachimi_display_set_brightness(0) && hachimi_display_get_brightness(&current) &&
                current >= HACHIMI_DISPLAY_MIN_PERCENT;
    bool restored = hachimi_display_set_brightness(original);
    CHECK(saved && safe && restored);
    printf("PASS brightness: saved value restored, 0 clamps to %d%%, original %d%% restored\n",
           current, original);
    return 0;
}

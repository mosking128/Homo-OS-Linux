#include "app_registry.h"

/* Borrowed product data must outlive the desktop. No concrete App dependencies. */
static hachimi_app_catalog_t catalog;
static bool running;

bool hachimi_app_registry_configure(const hachimi_app_catalog_t * config)
{
    if(running || config == NULL ||
       (config->home_count && !config->home) ||
       (config->dock_count && !config->dock) ||
       (config->background_count && !config->background)) return false;
    for(size_t i = 0; i < config->background_count; i++) {
        if(config->background[i] == NULL) return false;
        for(size_t j = 0; j < i; j++)
            if(config->background[j] == config->background[i]) return false;
    }
    catalog = *config;
    return true;
}

const hachimi_app_descriptor_t * hachimi_app_home_list(size_t * count)
{
    if(count) *count = catalog.home_count;
    return catalog.home;
}

const hachimi_app_descriptor_t * hachimi_app_dock_list(size_t * count)
{
    if(count) *count = catalog.dock_count;
    return catalog.dock;
}

void hachimi_app_system_start(void)
{
    if(running) return;
    running = true;
    for(size_t i = 0; i < catalog.background_count; i++)
        if(catalog.background[i]->on_system_start) catalog.background[i]->on_system_start();
}

void hachimi_app_system_stop(void)
{
    if(!running) return;
    for(size_t i = catalog.background_count; i > 0; i--)
        if(catalog.background[i-1]->on_system_stop) catalog.background[i-1]->on_system_stop();
    running = false;
}

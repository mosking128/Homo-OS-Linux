#ifndef HACHIMI_OFFLINE_MAP_H
#define HACHIMI_OFFLINE_MAP_H
#include <stdbool.h>
#include <stdint.h>
#define HACHIMI_MAP_TILE_BYTES (256U*256U*2U)
#define HACHIMI_MAP_CACHE_SIZE 32
#define HACHIMI_MAP_MAX_PLACES 512
typedef struct hachimi_map hachimi_map_t;
typedef struct {
    double lat, lon;
    char category[24], name[128], alias[256];
} hachimi_map_place_t;
typedef struct {
    unsigned ready, loading, missing, loads;
    uint32_t version;
} hachimi_map_stats_t;
hachimi_map_t * hachimi_map_open(const char * path);
void hachimi_map_close(hachimi_map_t * map);
const char * hachimi_map_region(hachimi_map_t * map);
void hachimi_map_center(hachimi_map_t * map, double * lat, double * lon);
void hachimi_map_clamp(hachimi_map_t * map, int zoom, int width, int height, double * x, double * y);
void hachimi_map_world(double lat, double lon, int zoom, double * x, double * y);
void hachimi_map_coords(double x, double y, int zoom, double * lat, double * lon);
double hachimi_map_distance(double lat1, double lon1, double lat2, double lon2);
unsigned hachimi_map_search(hachimi_map_t * map, const char * query, const char * category,
                            double lat, double lon, unsigned * results, unsigned capacity);
const hachimi_map_place_t * hachimi_map_place(hachimi_map_t * map, unsigned index);
void hachimi_map_begin_frame(hachimi_map_t * map);
/* UI thread only; returned pixels remain stable for the whole frame. No file
 * reads or LVGL calls occur here. The worker owns loading buffers exclusively. */
const uint8_t * hachimi_map_tile(hachimi_map_t * map, int z, int x, int y, int * slot, bool * settled);
void hachimi_map_stats(hachimi_map_t * map, hachimi_map_stats_t * stats);
#endif

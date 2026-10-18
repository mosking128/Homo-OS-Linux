#ifndef HACHIMI_PXP_ENGINE_H
#define HACHIMI_PXP_ENGINE_H
#include <stdint.h>
#include <stddef.h>

typedef struct hachimi_pxp hachimi_pxp_t;
hachimi_pxp_t * hachimi_pxp_open(const char * device, unsigned width, unsigned height);
void hachimi_pxp_close(hachimi_pxp_t * pxp);
void * hachimi_pxp_pixels(hachimi_pxp_t * pxp);
void * hachimi_pxp_scratch(hachimi_pxp_t * pxp);
uint32_t hachimi_pxp_scratch_address(hachimi_pxp_t * pxp);
int hachimi_pxp_worker_id(hachimi_pxp_t * pxp);
/* Destination stride is in bytes. The source is a rectangle in pixels(). */
int hachimi_pxp_blit(hachimi_pxp_t * pxp, unsigned x, unsigned y,
                     unsigned width, unsigned height, uint32_t destination,
                     unsigned stride, unsigned output_width, unsigned output_height);
#endif

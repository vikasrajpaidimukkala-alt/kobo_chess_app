#ifndef KOBOCHESS_DISPLAY_H
#define KOBOCHESS_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fbink.h"

typedef struct {
    int fbfd;
    FBInkConfig cfg;
    FBInkState state;
    unsigned char *pix;
    size_t pix_len;
    unsigned char *fb;
    size_t fb_size;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int fb_stride;
    uint32_t marker;
    bool color;
    bool fb_bgra;
} Display;

int display_init(Display *d);
void display_close(Display *d);
void display_put(Display *d, int x, int y, uint8_t r, uint8_t g, uint8_t b);
void display_fill_rect(Display *d, int x, int y, int w, int h,
                       uint8_t r, uint8_t g, uint8_t b);
void display_fill_circle(Display *d, int cx, int cy, int radius,
                         uint8_t r, uint8_t g, uint8_t b);
void display_stroke_circle(Display *d, int cx, int cy, int radius, int t,
                           uint8_t r, uint8_t g, uint8_t b);
void display_clear(Display *d, uint8_t r, uint8_t g, uint8_t b);
void display_hline(Display *d, int x0, int x1, int y, int t,
                   uint8_t r, uint8_t g, uint8_t b);
void display_vline(Display *d, int x, int y0, int y1, int t,
                   uint8_t r, uint8_t g, uint8_t b);
void display_text(Display *d, int x, int y, int scale, const char *s,
                  uint8_t r, uint8_t g, uint8_t b);
int display_text_width(const char *s, int scale);
int display_text_height(int scale);
void display_refresh(Display *d, int x, int y, int w, int h,
                     WFM_MODE_INDEX_T wfm, bool flash);
void display_refresh_full(Display *d, bool flash);

#endif

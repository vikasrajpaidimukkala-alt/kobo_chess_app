#ifndef KOBOCHESS_CANVAS_H
#define KOBOCHESS_CANVAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * An off-screen RGBA8888 bitmap. Everything above the platform layer
 * draws here and nowhere else, which is what keeps the game, the
 * engine and the UI free of device knowledge. A backend turns this
 * into whatever its panel wants in its present() call.
 */
typedef struct {
    unsigned char *pix;   /* width * height * 4, row stride is `stride` */
    size_t len;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
} Canvas;

bool canvas_init(Canvas *c, unsigned int width, unsigned int height);
void canvas_free(Canvas *c);

void canvas_put(Canvas *c, int x, int y, uint8_t r, uint8_t g, uint8_t b);
void canvas_fill_rect(Canvas *c, int x, int y, int w, int h,
                      uint8_t r, uint8_t g, uint8_t b);
void canvas_fill_circle(Canvas *c, int cx, int cy, int radius,
                        uint8_t r, uint8_t g, uint8_t b);
void canvas_stroke_circle(Canvas *c, int cx, int cy, int radius, int t,
                          uint8_t r, uint8_t g, uint8_t b);
void canvas_clear(Canvas *c, uint8_t r, uint8_t g, uint8_t b);
void canvas_hline(Canvas *c, int x0, int x1, int y, int t,
                  uint8_t r, uint8_t g, uint8_t b);
void canvas_vline(Canvas *c, int x, int y0, int y1, int t,
                  uint8_t r, uint8_t g, uint8_t b);

/* Bundled 8x8 bitmap font, scaled by whole pixels. */
void canvas_text(Canvas *c, int x, int y, int scale, const char *s,
                 uint8_t r, uint8_t g, uint8_t b);
int canvas_text_width(const char *s, int scale);
int canvas_text_height(int scale);

#endif

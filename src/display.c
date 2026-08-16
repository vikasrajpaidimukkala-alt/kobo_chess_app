#include "display.h"
#include "hwtcon_kobo.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * Packed RGBA offscreen, copy into the native framebuffer, then
 * HWTCON_SEND_UPDATE with GCC16 + CFA_G2.
 *
 * Libra Colour's MDP defaults to whatever Nickel last set. KOReader
 * forces ABGR32 via mdp_src_format before painting; without that, CFA
 * treats our bytes as the wrong layout and leaves jagged wedges.
 */

static const unsigned char FONT8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}, /* ! */
    {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, /* # */
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, /* $ */
    {0x62,0x66,0x0C,0x18,0x30,0x66,0x46,0x00}, /* % */
    {0x38,0x6C,0x38,0x70,0xDE,0xCC,0x76,0x00}, /* & */
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, /* ( */
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, /* ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* * */
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, /* , */
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, /* . */
    {0x02,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* / */
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, /* 0 */
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, /* 1 */
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, /* 2 */
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, /* 3 */
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}, /* 4 */
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, /* 5 */
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, /* 6 */
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, /* 7 */
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, /* 8 */
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, /* 9 */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, /* : */
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, /* ; */
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, /* < */
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, /* = */
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, /* > */
    {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00}, /* ? */
    {0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x3C,0x00}, /* @ */
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}, /* A */
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, /* B */
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, /* C */
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, /* D */
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}, /* E */
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00}, /* F */
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00}, /* G */
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, /* H */
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}, /* I */
    {0x3E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}, /* J */
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, /* K */
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, /* L */
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, /* M */
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}, /* N */
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, /* O */
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, /* P */
    {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00}, /* Q */
    {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00}, /* R */
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, /* S */
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, /* T */
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, /* U */
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, /* V */
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* W */
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, /* X */
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, /* Y */
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, /* Z */
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, /* [ */
    {0x40,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, /* \ */
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, /* ] */
    {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* _ */
    {0x18,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}, /* a */
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, /* b */
    {0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00}, /* c */
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, /* d */
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}, /* e */
    {0x1C,0x30,0x30,0x7C,0x30,0x30,0x30,0x00}, /* f */
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C}, /* g */
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, /* h */
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, /* i */
    {0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x6C,0x38}, /* j */
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00}, /* k */
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, /* l */
    {0x00,0x00,0x76,0x7F,0x6B,0x63,0x63,0x00}, /* m */
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, /* n */
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00}, /* o */
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, /* p */
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06}, /* q */
    {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00}, /* r */
    {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}, /* s */
    {0x30,0x30,0x7C,0x30,0x30,0x30,0x1C,0x00}, /* t */
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}, /* u */
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, /* v */
    {0x00,0x00,0x63,0x6B,0x7F,0x3E,0x36,0x00}, /* w */
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00}, /* x */
    {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C}, /* y */
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, /* z */
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, /* { */
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, /* | */
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, /* } */
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}, /* ~ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

static void kobo_mtk_set_mdp_format(const char *fmt)
{
    DIR *dir;
    struct dirent *ent;

    dir = opendir("/sys/devices/platform");
    if (dir == NULL) {
        fprintf(stderr, "opendir platform: errno=%d\n", errno);
        return;
    }

    while ((ent = readdir(dir)) != NULL) {
        char path[256];
        int fd;
        size_t n;

        if (strstr(ent->d_name, "hwtcon") == NULL) {
            continue;
        }

        snprintf(path, sizeof(path),
                 "/sys/devices/platform/%s/mdp_src_format", ent->d_name);
        fd = open(path, O_WRONLY | O_CLOEXEC);
        if (fd < 0) {
            fprintf(stderr, "open %s: errno=%d\n", path, errno);
            continue;
        }

        n = strlen(fmt);
        if (write(fd, fmt, n) < 0) {
            fprintf(stderr, "write %s: errno=%d\n", path, errno);
        } else {
            fprintf(stderr, "mdp_src_format %s <- %s\n", path, fmt);
        }
        close(fd);
    }

    closedir(dir);
}

static void copy_pix_to_fb(Display *d)
{
    unsigned int yy;

    for (yy = 0; yy < d->height; yy++) {
        unsigned char *dst = d->fb + ((size_t)yy * d->fb_stride);
        const unsigned char *src = d->pix + ((size_t)yy * d->stride);

        if (!d->fb_bgra) {
            memcpy(dst, src, d->stride);
            continue;
        }

        {
            unsigned int xx;

            for (xx = 0; xx < d->width; xx++) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
                dst += 4;
                src += 4;
            }
        }
    }

    if (msync(d->fb, d->fb_size, MS_SYNC) < 0) {
        fprintf(stderr, "msync: errno=%d\n", errno);
    }
}

int display_init(Display *d)
{
    size_t required;
    int rc;

    memset(d, 0, sizeof(*d));
    d->fbfd = -1;
    d->cfg.is_quiet = true;
    d->cfg.ignore_alpha = true;

    d->fbfd = fbink_open();
    if (d->fbfd < 0) {
        fprintf(stderr, "fbink_open() failed: %d\n", d->fbfd);
        return -1;
    }

    rc = fbink_init(d->fbfd, &d->cfg);
    if (rc < 0) {
        fprintf(stderr, "fbink_init() failed: %d\n", rc);
        fbink_close(d->fbfd);
        d->fbfd = -1;
        return -1;
    }

    kobo_mtk_set_mdp_format("ABGR32");
    rc = fbink_reinit(d->fbfd, &d->cfg);
    if (rc < 0) {
        fprintf(stderr, "fbink_reinit() after mdp_src_format: %d\n", rc);
    }

    fbink_get_state(&d->cfg, &d->state);

    d->width = d->state.screen_width;
    d->height = d->state.screen_height;
    d->stride = d->width * 4U;
    d->fb_stride = d->state.scanline_stride;
    d->color = d->state.has_color_panel;
    d->fb_bgra = (d->state.pixel_format == FBINK_PXFMT_BGRA ||
                  d->state.pixel_format == FBINK_PXFMT_BGR32);

    if (d->state.bpp != 32) {
        fprintf(stderr, "Need a 32-bit framebuffer (got %u bpp)\n", d->state.bpp);
        fbink_close(d->fbfd);
        d->fbfd = -1;
        return -1;
    }

    d->pix_len = (size_t)d->stride * d->height;
    d->pix = malloc(d->pix_len);
    if (d->pix == NULL) {
        fprintf(stderr, "Failed to allocate %zu-byte bitmap\n", d->pix_len);
        fbink_close(d->fbfd);
        d->fbfd = -1;
        return -1;
    }

    memset(d->pix, 0xFF, d->pix_len);

    d->fb = fbink_get_fb_pointer(d->fbfd, &d->fb_size);
    if (d->fb == NULL) {
        fprintf(stderr, "fbink_get_fb_pointer() failed\n");
        free(d->pix);
        d->pix = NULL;
        fbink_close(d->fbfd);
        d->fbfd = -1;
        return -1;
    }

    required = (size_t)d->fb_stride * d->height;
    if (d->fb_size < required) {
        fprintf(stderr, "Framebuffer too small: %zu < %zu\n", d->fb_size, required);
        free(d->pix);
        d->pix = NULL;
        fbink_close(d->fbfd);
        d->fbfd = -1;
        return -1;
    }

    d->marker = 0U;

    {
        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;

        memset(&vinfo, 0, sizeof(vinfo));
        memset(&finfo, 0, sizeof(finfo));
        fbink_get_fb_info(&vinfo, &finfo);
        fprintf(stderr, "======== KOBOCHESS_DRAW v6 mdp-abgr32+cfa-g2 ========\n");
        fprintf(stderr, "Device: %s (%s)\n",
                d->state.device_name, d->state.device_codename);
        fprintf(stderr, "Screen: %u x %u, fb_stride %u, pixfmt %u bgra=%d, color %d, mtk %d\n",
                d->width, d->height, d->fb_stride, d->state.pixel_format,
                (int)d->fb_bgra, (int)d->color, (int)d->state.is_mtk);
        fprintf(stderr, "Native fb: %u x %u, rotate %u, line_len %u, rgb offsets %u/%u/%u\n",
                vinfo.xres, vinfo.yres, vinfo.rotate, finfo.line_length,
                vinfo.red.offset, vinfo.green.offset, vinfo.blue.offset);
    }

    return 0;
}

void display_close(Display *d)
{
    free(d->pix);
    d->pix = NULL;
    d->fb = NULL;

    if (d->fbfd >= 0) {
        fbink_close(d->fbfd);
        d->fbfd = -1;
    }
}

void display_put(Display *d, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    unsigned char *pixel;

    if ((unsigned)x >= d->width || (unsigned)y >= d->height) {
        return;
    }

    pixel = d->pix + ((size_t)y * d->stride) + ((size_t)x * 4);
    pixel[0] = r;
    pixel[1] = g;
    pixel[2] = b;
    pixel[3] = 0xFF;
}

void display_fill_rect(Display *d, int x, int y, int w, int h,
                       uint8_t r, uint8_t g, uint8_t b)
{
    int yy;
    int xx;
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > (int)d->width) {
        x1 = (int)d->width;
    }
    if (y1 > (int)d->height) {
        y1 = (int)d->height;
    }

    for (yy = y0; yy < y1; yy++) {
        unsigned char *pixel =
            d->pix + ((size_t)yy * d->stride) + ((size_t)x0 * 4);

        for (xx = x0; xx < x1; xx++) {
            pixel[0] = r;
            pixel[1] = g;
            pixel[2] = b;
            pixel[3] = 0xFF;
            pixel += 4;
        }
    }
}

void display_fill_circle(Display *d, int cx, int cy, int radius,
                         uint8_t r, uint8_t g, uint8_t b)
{
    int y;
    int rr = radius * radius;

    for (y = -radius; y <= radius; y++) {
        int x;
        int yy = y * y;

        for (x = -radius; x <= radius; x++) {
            if (x * x + yy <= rr) {
                display_put(d, cx + x, cy + y, r, g, b);
            }
        }
    }
}

void display_stroke_circle(Display *d, int cx, int cy, int radius, int t,
                           uint8_t r, uint8_t g, uint8_t b)
{
    int y;
    int r2 = radius * radius;
    int inner = radius - t;
    int i2 = (inner < 0) ? 0 : inner * inner;

    for (y = -radius; y <= radius; y++) {
        int x;
        int yy = y * y;

        for (x = -radius; x <= radius; x++) {
            int d2 = x * x + yy;

            if (d2 <= r2 && d2 >= i2) {
                display_put(d, cx + x, cy + y, r, g, b);
            }
        }
    }
}

void display_clear(Display *d, uint8_t r, uint8_t g, uint8_t b)
{
    display_fill_rect(d, 0, 0, (int)d->width, (int)d->height, r, g, b);
}

void display_hline(Display *d, int x0, int x1, int y, int t,
                   uint8_t r, uint8_t g, uint8_t b)
{
    if (x1 < x0) {
        int tmp = x0;
        x0 = x1;
        x1 = tmp;
    }

    display_fill_rect(d, x0, y, x1 - x0 + 1, t, r, g, b);
}

void display_vline(Display *d, int x, int y0, int y1, int t,
                   uint8_t r, uint8_t g, uint8_t b)
{
    if (y1 < y0) {
        int tmp = y0;
        y0 = y1;
        y1 = tmp;
    }

    display_fill_rect(d, x, y0, t, y1 - y0 + 1, r, g, b);
}

static const unsigned char *glyph_for(char c)
{
    unsigned char u = (unsigned char)c;

    if (u < 32 || u > 127) {
        u = '?';
    }

    return FONT8[u - 32];
}

int display_text_width(const char *s, int scale)
{
    return (int)strlen(s) * 8 * scale;
}

int display_text_height(int scale)
{
    return 8 * scale;
}

void display_text(Display *d, int x, int y, int scale, const char *s,
                  uint8_t r, uint8_t g, uint8_t b)
{
    int cx = x;
    int i;

    if (scale < 1) {
        scale = 1;
    }

    for (i = 0; s[i] != '\0'; i++) {
        const unsigned char *gly = glyph_for(s[i]);
        int row;
        int col;

        for (row = 0; row < 8; row++) {
            unsigned char bits = gly[row];
            for (col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    display_fill_rect(d,
                                      cx + col * scale,
                                      y + row * scale,
                                      scale, scale, r, g, b);
                }
            }
        }

        cx += 8 * scale;
    }
}

void display_refresh(Display *d, int x, int y, int w, int h,
                     WFM_MODE_INDEX_T wfm, bool flash)
{
    int rc;

    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)wfm;

    copy_pix_to_fb(d);

    if (d->state.is_mtk) {
        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;
        struct hwtcon_update_data upd;

        memset(&vinfo, 0, sizeof(vinfo));
        memset(&finfo, 0, sizeof(finfo));
        fbink_get_fb_info(&vinfo, &finfo);

        /*
         * Kaleido GCC16 must be FULL and carry CFA_G2. Without that
         * flag the MTK driver uses the wrong working buffer.
         */
        memset(&upd, 0, sizeof(upd));
        upd.update_region.top = 0;
        upd.update_region.left = 0;
        upd.update_region.width = vinfo.xres;
        upd.update_region.height = vinfo.yres;
        upd.waveform_mode = d->color ? HWTCON_WAVEFORM_MODE_GCC16
                                     : HWTCON_WAVEFORM_MODE_GC16;
        upd.update_mode = UPDATE_MODE_FULL;
        if (d->color) {
            upd.flags = HWTCON_FLAG_CFA_EINK_G2;
        }
        upd.dither_mode = 0;
        (void)flash;

        if (d->marker != 0U) {
            (void)ioctl(d->fbfd, HWTCON_WAIT_FOR_UPDATE_COMPLETE, &d->marker);
        }

        d->marker++;
        if (d->marker == 0U) {
            d->marker = 1U;
        }
        upd.update_marker = d->marker;

        fprintf(stderr,
                "hwtcon SEND_UPDATE %ux%u rotate=%u wfm=%u flags=0x%x marker=%u\n",
                vinfo.xres, vinfo.yres, vinfo.rotate,
                upd.waveform_mode, upd.flags, upd.update_marker);

        rc = ioctl(d->fbfd, HWTCON_SEND_UPDATE, &upd);
        if (rc < 0) {
            fprintf(stderr, "HWTCON_SEND_UPDATE failed: errno=%d\n", errno);
            return;
        }

        (void)ioctl(d->fbfd, HWTCON_WAIT_FOR_UPDATE_SUBMISSION, &d->marker);
        (void)ioctl(d->fbfd, HWTCON_WAIT_FOR_UPDATE_COMPLETE, &d->marker);
        return;
    }

    {
        FBInkConfig cfg = d->cfg;

        cfg.wfm_mode = wfm;
        cfg.is_flashing = flash;
        cfg.dithering_mode = HWD_PASSTHROUGH;
        rc = fbink_refresh(d->fbfd, 0, 0, 0, 0, &cfg);
        if (rc < 0) {
            fprintf(stderr, "fbink_refresh() fullscreen failed: %d\n", rc);
        }
    }
}

void display_refresh_full(Display *d, bool flash)
{
    WFM_MODE_INDEX_T wfm;

    if (d->color) {
        wfm = WFM_GCC16;
        flash = true;
    } else {
        wfm = WFM_GC16;
    }

    display_refresh(d, 0, 0, 0, 0, wfm, flash);
}

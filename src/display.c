#include "display.h"

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
 * Packed gray offscreen, memcpy into the framebuffer, then FBInk
 * full-screen GC16. Fence each EPDC update with wait_for_submission
 * + wait_for_complete before touching the framebuffer again: writing
 * the next frame while a waveform is still in flight produces jagged
 * old/new bands on Kaleido 3.
 *
 * Do not send HWTCON_FLAG_CFA_SKIP at 32bpp: that makes the MTK
 * driver mix in the previous working buffer as jagged tears.
 */

#ifndef LAST_MARKER
#define LAST_MARKER 0U
#endif

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

static void restore_nickel_fb(int fbfd, FBInkConfig *cfg)
{
    kobo_mtk_set_mdp_format("ABGR32");
    if (fbfd >= 0) {
        (void)fbink_reinit(fbfd, cfg);
    }
}

static void copy_pix_to_fb(Display *d)
{
    unsigned int yy;

    for (yy = 0; yy < d->height; yy++) {
        unsigned char *dst = d->fb + ((size_t)yy * d->fb_stride);
        const unsigned char *src = d->pix + ((size_t)yy * d->stride);

        if (d->fb_y8) {
            unsigned int xx;

            for (xx = 0; xx < d->width; xx++) {
                dst[xx] = src[0];
                src += 4;
            }
            continue;
        }

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
}

static void wait_epdc(Display *d, const char *tag)
{
    int rc;

    if (d->state.can_wait_for_submission) {
        rc = fbink_wait_for_submission(d->fbfd, LAST_MARKER);
        fprintf(stderr, "fbink_wait_for_submission %s rc=%d\n", tag, rc);
    }

    rc = fbink_wait_for_complete(d->fbfd, LAST_MARKER);
    fprintf(stderr, "fbink_wait_for_complete %s rc=%d\n", tag, rc);
}

static int refresh_gc16_wait(Display *d, const char *tag)
{
    FBInkConfig cfg = d->cfg;
    char before[64];
    char after[64];
    int rc;

    snprintf(before, sizeof(before), "before-%s", tag);
    snprintf(after, sizeof(after), "after-%s", tag);

    /*
     * The EPDC is still walking the previous waveform. Do not rewrite
     * the mmap'd framebuffer until that update has finished.
     */
    wait_epdc(d, before);

    copy_pix_to_fb(d);

    cfg.wfm_mode = WFM_GC16;
    cfg.is_flashing = true;
    cfg.dithering_mode = HWD_PASSTHROUGH;
    rc = fbink_refresh(d->fbfd, 0, 0, 0, 0, &cfg);
    fprintf(stderr, "fbink_refresh %s gc16 flash=1 rc=%d\n", tag, rc);

    wait_epdc(d, after);
    return rc;
}

static void hwtcon_cmd(const char *cmd)
{
    int fd = open("/proc/hwtcon/cmd", O_WRONLY | O_CLOEXEC);

    if (fd < 0) {
        fprintf(stderr, "open /proc/hwtcon/cmd: errno=%d\n", errno);
        return;
    }

    if (write(fd, cmd, strlen(cmd)) < 0) {
        fprintf(stderr, "hwtcon cmd '%s': errno=%d\n", cmd, errno);
    } else {
        fprintf(stderr, "hwtcon cmd '%s' ok\n", cmd);
    }
    close(fd);
}

int display_init(Display *d)
{
    size_t required;
    int rc;

    memset(d, 0, sizeof(*d));
    d->fbfd = -1;
    d->cfg.is_quiet = true;
    d->cfg.ignore_alpha = true;
    d->cfg.bg_color = BG_WHITE;

    /*
     * Libra Colour's fb is already portrait. FBInk image/coord rotation
     * would shear an already-upright buffer.
     */
    (void)setenv("FBINK_NO_SW_ROTA", "1", 1);

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
    d->color = false;
    d->fb_y8 = (d->state.bpp == 8);
    d->fb_bgra = (d->state.pixel_format == FBINK_PXFMT_BGRA ||
                  d->state.pixel_format == FBINK_PXFMT_BGR32);

    if (d->state.bpp != 8 && d->state.bpp != 32) {
        fprintf(stderr, "Need 8- or 32-bit framebuffer (got %u bpp)\n",
                d->state.bpp);
        restore_nickel_fb(d->fbfd, &d->cfg);
        fbink_close(d->fbfd);
        d->fbfd = -1;
        return -1;
    }

    d->pix_len = (size_t)d->stride * d->height;
    d->pix = malloc(d->pix_len);
    if (d->pix == NULL) {
        fprintf(stderr, "Failed to allocate %zu-byte bitmap\n", d->pix_len);
        restore_nickel_fb(d->fbfd, &d->cfg);
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
        restore_nickel_fb(d->fbfd, &d->cfg);
        fbink_close(d->fbfd);
        d->fbfd = -1;
        return -1;
    }

    required = (size_t)d->fb_stride * d->height;
    if (d->fb_size < required) {
        fprintf(stderr, "Framebuffer too small: %zu < %zu\n", d->fb_size, required);
        free(d->pix);
        d->pix = NULL;
        restore_nickel_fb(d->fbfd, &d->cfg);
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
        fprintf(stderr, "======== KOBOCHESS_DRAW v12 wait-fence ========\n");
        fprintf(stderr, "Device: %s (%s)\n",
                d->state.device_name, d->state.device_codename);
        fprintf(stderr, "Screen: %u x %u, fb_stride %u, bpp %u pixfmt %u y8=%d bgra=%d, panel_color %d, mtk %d\n",
                d->width, d->height, d->fb_stride, d->state.bpp,
                d->state.pixel_format, (int)d->fb_y8, (int)d->fb_bgra,
                (int)d->state.has_color_panel, (int)d->state.is_mtk);
        fprintf(stderr, "Native fb: %u x %u, rotate %u, line_len %u, rgb offsets %u/%u/%u smem %u\n",
                vinfo.xres, vinfo.yres, vinfo.rotate, finfo.line_length,
                vinfo.red.offset, vinfo.green.offset, vinfo.blue.offset,
                finfo.smem_len);
        fprintf(stderr, "can_wait_for_submission=%d unreliable_wait_for=%d\n",
                (int)d->state.can_wait_for_submission,
                (int)d->state.unreliable_wait_for);

        if (d->state.is_mtk) {
            hwtcon_cmd("night_mode 0");
            hwtcon_cmd("fiti_power 1");
        }

        /*
         * Diagnostic: white / black / white, each fenced, then the
         * chessboard paint in main. If the board is clean after this,
         * the jagged bands were refresh overlap, not drawing.
         */
        fprintf(stderr, "sync-test WHITE\n");
        display_clear(d, 0xFF, 0xFF, 0xFF);
        refresh_gc16_wait(d, "white1");

        fprintf(stderr, "sync-test BLACK\n");
        display_clear(d, 0x00, 0x00, 0x00);
        refresh_gc16_wait(d, "black");

        fprintf(stderr, "sync-test WHITE\n");
        display_clear(d, 0xFF, 0xFF, 0xFF);
        refresh_gc16_wait(d, "white2");
    }

    return 0;
}

void display_close(Display *d)
{
    free(d->pix);
    d->pix = NULL;
    d->fb = NULL;

    if (d->fbfd >= 0) {
        restore_nickel_fb(d->fbfd, &d->cfg);
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
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)wfm;
    (void)flash;

    refresh_gc16_wait(d, "chess");
}

void display_refresh_full(Display *d, bool flash)
{
    (void)flash;
    refresh_gc16_wait(d, "chess");
}

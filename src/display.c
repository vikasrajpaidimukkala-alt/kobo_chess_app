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
 * Packed gray offscreen, 2x2-average into the framebuffer, then a
 * full-screen GC16 update.
 *
 * Kaleido's colour filter is a physical layer. Grayscale does not
 * remove it. Chessboards and piece outlines beat against that mosaic
 * and show up as jagged wedges through the back ranks. Averaging each
 * 2x2 cell is what Nickel's "reduce rainbow" pass does.
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

static void restore_nickel_fb(int fbfd, FBInkConfig *cfg)
{
    kobo_mtk_set_mdp_format("ABGR32");
    if (fbfd >= 0) {
        (void)fbink_reinit(fbfd, cfg);
    }
}

static unsigned char pix_gray(const Display *d, unsigned int x, unsigned int y)
{
    const unsigned char *src =
        d->pix + ((size_t)y * d->stride) + ((size_t)x * 4);

    return src[0];
}

static void copy_pix_to_fb(Display *d)
{
    unsigned int yy;

    for (yy = 0; yy < d->height; yy++) {
        unsigned char *dst = d->fb + ((size_t)yy * d->fb_stride);
        unsigned int y1 = (yy + 1U < d->height) ? (yy + 1U) : yy;
        unsigned int xx;

        for (xx = 0; xx < d->width; xx++) {
            unsigned int x1 = (xx + 1U < d->width) ? (xx + 1U) : xx;
            unsigned int sum = (unsigned int)pix_gray(d, xx, yy) +
                               (unsigned int)pix_gray(d, x1, yy) +
                               (unsigned int)pix_gray(d, xx, y1) +
                               (unsigned int)pix_gray(d, x1, y1);
            unsigned char g = (unsigned char)(sum / 4U);

            if (d->fb_y8) {
                dst[xx] = g;
            } else {
                unsigned char *px = dst + ((size_t)xx * 4);

                px[0] = g;
                px[1] = g;
                px[2] = g;
                px[3] = 0xFF;
            }
        }
    }
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

static void hwtcon_wait_complete(Display *d, uint32_t marker)
{
    struct hwtcon_update_marker_data md;
    int rc;

    md.update_marker = marker;
    md.collision_test = 0;
    rc = ioctl(d->fbfd, HWTCON_WAIT_FOR_UPDATE_COMPLETE, &md);
    if (rc < 0) {
        fprintf(stderr, "WAIT_COMPLETE marker=%u: errno=%d\n", marker, errno);
    }
}

static int hwtcon_send(Display *d, uint32_t wfm, unsigned int flags,
                       const char *tag)
{
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    struct hwtcon_update_data upd;
    int rc;

    memset(&vinfo, 0, sizeof(vinfo));
    memset(&finfo, 0, sizeof(finfo));
    fbink_get_fb_info(&vinfo, &finfo);

    memset(&upd, 0, sizeof(upd));
    upd.update_region.top = 0;
    upd.update_region.left = 0;
    upd.update_region.width = vinfo.xres;
    upd.update_region.height = vinfo.yres;
    upd.waveform_mode = wfm;
    upd.update_mode = UPDATE_MODE_FULL;
    upd.flags = flags;
    upd.dither_mode = 0;

    if (d->marker != 0U) {
        hwtcon_wait_complete(d, d->marker);
    }

    d->marker++;
    if (d->marker == 0U) {
        d->marker = 1U;
    }
    upd.update_marker = d->marker;

    fprintf(stderr,
            "hwtcon %s SEND_UPDATE %ux%u rotate=%u wfm=%u flags=0x%x marker=%u\n",
            tag, vinfo.xres, vinfo.yres, vinfo.rotate,
            upd.waveform_mode, upd.flags, upd.update_marker);

    rc = ioctl(d->fbfd, HWTCON_SEND_UPDATE, &upd);
    if (rc < 0) {
        fprintf(stderr, "HWTCON_SEND_UPDATE %s failed: errno=%d\n", tag, errno);
        return -1;
    }

    rc = ioctl(d->fbfd, HWTCON_WAIT_FOR_UPDATE_SUBMISSION, &d->marker);
    if (rc < 0) {
        fprintf(stderr, "WAIT_SUBMISSION %s marker=%u: errno=%d\n",
                tag, d->marker, errno);
    }
    hwtcon_wait_complete(d, d->marker);
    return 0;
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

    kobo_mtk_set_mdp_format("Y8");
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
        uint32_t cfa_mode = HWTCON_CFA_MODE_NONE;

        memset(&vinfo, 0, sizeof(vinfo));
        memset(&finfo, 0, sizeof(finfo));
        fbink_get_fb_info(&vinfo, &finfo);
        fprintf(stderr, "======== KOBOCHESS_DRAW v9 gray-gc16+cfa-blur ========\n");
        fprintf(stderr, "Device: %s (%s)\n",
                d->state.device_name, d->state.device_codename);
        fprintf(stderr, "Screen: %u x %u, fb_stride %u, bpp %u pixfmt %u y8=%d bgra=%d, panel_color %d, mtk %d\n",
                d->width, d->height, d->fb_stride, d->state.bpp,
                d->state.pixel_format, (int)d->fb_y8, (int)d->fb_bgra,
                (int)d->state.has_color_panel, (int)d->state.is_mtk);
        fprintf(stderr, "Native fb: %u x %u, rotate %u, line_len %u, rgb offsets %u/%u/%u\n",
                vinfo.xres, vinfo.yres, vinfo.rotate, finfo.line_length,
                vinfo.red.offset, vinfo.green.offset, vinfo.blue.offset);

        if (d->state.is_mtk) {
            hwtcon_cmd("night_mode 0");
            hwtcon_cmd("fiti_power 1");
            rc = ioctl(d->fbfd, HWTCON_SET_CFA_MODE, &cfa_mode);
            if (rc < 0) {
                fprintf(stderr, "HWTCON_SET_CFA_MODE NONE: errno=%d\n", errno);
            } else {
                fprintf(stderr, "HWTCON_SET_CFA_MODE NONE ok\n");
            }

            copy_pix_to_fb(d);
            (void)hwtcon_send(d, HWTCON_WAVEFORM_MODE_GC16,
                              d->fb_y8 ? 0U : HWTCON_FLAG_CFA_SKIP,
                              "gc16-white");
        }
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
    int rc;

    (void)x;
    (void)y;
    (void)w;
    (void)h;

    copy_pix_to_fb(d);

    if (d->state.is_mtk) {
        unsigned int flags = 0U;

        if (!d->fb_y8) {
            flags = HWTCON_FLAG_CFA_SKIP;
        }
        (void)hwtcon_send(d, HWTCON_WAVEFORM_MODE_GC16, flags, "gc16");
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
    display_refresh(d, 0, 0, 0, 0, WFM_GC16, flash);
}

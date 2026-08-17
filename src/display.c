#include "display.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Packed gray offscreen, memcpy into the mmap'd RGBA framebuffer,
 * then a Kaleido refresh (GCC16 + CFA G2), fenced on the update
 * marker returned by FBInk.
 *
 * The jagged bands through the back ranks survived GC16, GCC16, Y8,
 * a homemade ioctl, and a full submission/completion fence, so the
 * first paint of a freshly written buffer already tears. The dump
 * below exists to settle whether those bytes leave here intact, and
 * the waveform is env-selectable so it can be bisected on-device.
 *
 * Do not poke mdp_src_format. Do not send CFA_SKIP at 32bpp.
 */

#ifndef LAST_MARKER
#define LAST_MARKER 0U
#endif

#define DUMP_DIR "/mnt/onboard/.adds/kobochess"

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

static void restore_nickel_fb(int fbfd, FBInkConfig *cfg)
{
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

/*
 * The four-corner test (src/fb_direct_test.c) is the only draw this
 * panel has ever rendered cleanly, and it refreshed with a zeroed
 * config: AUTO, no flash, no CFA. Default to exactly that, and allow
 * overriding from the launcher so the waveform can be bisected
 * without a rebuild.
 */
static WFM_MODE_INDEX_T g_wfm = WFM_AUTO;
static CFA_MODE_INDEX_T g_cfa = CFA_DEFAULT;
static bool g_flash = false;

static void refresh_mode_init(void)
{
    const char *wfm = getenv("KOBOCHESS_WFM");
    const char *flash = getenv("KOBOCHESS_FLASH");
    const char *cfa = getenv("KOBOCHESS_CFA");

    if (wfm != NULL) {
        if (strcmp(wfm, "gc16") == 0) {
            g_wfm = WFM_GC16;
        } else if (strcmp(wfm, "gcc16") == 0) {
            g_wfm = WFM_GCC16;
        } else if (strcmp(wfm, "glrc16") == 0) {
            g_wfm = WFM_GLRC16;
        } else if (strcmp(wfm, "gl16") == 0) {
            g_wfm = WFM_GL16;
        }
    }

    if (flash != NULL) {
        g_flash = (atoi(flash) != 0);
    }

    if (cfa != NULL && strcmp(cfa, "g2") == 0) {
        g_cfa = CFA_G2;
    }

    fprintf(stderr, "refresh mode: wfm=%u flash=%d cfa=%u\n",
            (unsigned)g_wfm, (int)g_flash, (unsigned)g_cfa);
}

static void wait_marker(Display *d, uint32_t marker, const char *tag)
{
    int rc;

    if (marker == LAST_MARKER) {
        return;
    }

    if (d->state.can_wait_for_submission) {
        rc = fbink_wait_for_submission(d->fbfd, marker);
        fprintf(stderr, "wait_for_submission %s marker=%u rc=%d\n",
                tag, marker, rc);
    }

    rc = fbink_wait_for_complete(d->fbfd, marker);
    fprintf(stderr, "wait_for_complete %s marker=%u rc=%d\n", tag, marker, rc);
}

/*
 * Write the red channel of a 4-byte-per-pixel buffer as a PGM, so the
 * exact bytes the panel was handed can be inspected off-device.
 */
static void dump_pgm(const char *path, const unsigned char *src,
                     unsigned int width, unsigned int height,
                     unsigned int stride)
{
    FILE *f = fopen(path, "wb");
    unsigned char *line;
    unsigned int y;

    if (f == NULL) {
        fprintf(stderr, "dump %s: errno=%d\n", path, errno);
        return;
    }

    line = malloc(width);
    if (line == NULL) {
        fclose(f);
        return;
    }

    fprintf(f, "P5\n%u %u\n255\n", width, height);
    for (y = 0; y < height; y++) {
        const unsigned char *row = src + ((size_t)y * stride);
        unsigned int x;

        for (x = 0; x < width; x++) {
            line[x] = row[(size_t)x * 4];
        }
        fwrite(line, 1, width, f);
    }

    free(line);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    fprintf(stderr, "dumped %s\n", path);
}

/*
 * Does the framebuffer still hold what we wrote once the panel is
 * done? If the driver's CFA pass writes back over our scanlines, the
 * tears are the controller's, not ours.
 */
static void verify_fb(Display *d)
{
    unsigned int y;
    unsigned int bad = 0;
    int first = -1;
    int last = -1;

    if (d->fb_y8 || d->fb_bgra) {
        return;
    }

    for (y = 0; y < d->height; y++) {
        const unsigned char *fbrow = d->fb + ((size_t)y * d->fb_stride);
        const unsigned char *pixrow = d->pix + ((size_t)y * d->stride);

        if (memcmp(fbrow, pixrow, d->stride) != 0) {
            bad++;
            if (first < 0) {
                first = (int)y;
            }
            last = (int)y;
        }
    }

    fprintf(stderr, "fb readback: %u/%u rows differ (first %d last %d)\n",
            bad, d->height, first, last);
}

static int refresh_panel_wait(Display *d, const char *tag)
{
    static int paint = 0;
    FBInkConfig cfg = d->cfg;
    FBInkRect rect;
    int rc;

    /* Fence the previous update before touching the mmap again. */
    wait_marker(d, d->marker, "prev");

    copy_pix_to_fb(d);

    cfg.wfm_mode = g_wfm;
    cfg.is_flashing = g_flash;
    cfg.dithering_mode = HWD_PASSTHROUGH;
    cfg.cfa_mode = g_cfa;

    rect.left = 0;
    rect.top = 0;
    rect.width = (unsigned short)d->width;
    rect.height = (unsigned short)d->height;
    rc = fbink_refresh_rect(d->fbfd, &rect, &cfg);

    d->marker = fbink_get_last_marker();
    fprintf(stderr, "refresh %s wfm=%u flash=%d %ux%u rc=%d marker=%u\n",
            tag, (unsigned)g_wfm, (int)g_flash, d->width, d->height,
            rc, d->marker);

    wait_marker(d, d->marker, tag);

    if (paint == 0) {
        verify_fb(d);
        dump_pgm(DUMP_DIR "/dump_pix.pgm", d->pix, d->width, d->height,
                 d->stride);
        dump_pgm(DUMP_DIR "/dump_fb.pgm", d->fb, d->width, d->height,
                 d->fb_stride);
    }
    paint++;

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

    refresh_mode_init();

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
        fprintf(stderr, "======== KOBOCHESS_DRAW v14 marker-fence-dump ========\n");
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

    refresh_panel_wait(d, "chess");
}

void display_refresh_full(Display *d, bool flash)
{
    (void)flash;
    refresh_panel_wait(d, "chess");
}

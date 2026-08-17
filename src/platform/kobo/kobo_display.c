#include "kobo.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * Kobo / MediaTek hwtcon display backend, developed against a Libra
 * Colour (Monza, Kaleido 3, 1264x1680 @ 32bpp RGBA).
 *
 * The canvas is memcpy'd into the mmap'd framebuffer, then handed to
 * the panel with one full-screen update which we wait out before
 * returning. Notes for anyone porting this to another Kobo:
 *
 *   - Every refresh flashes with GC16. Partial updates leave grey
 *     ghosts of the previous position on this Kaleido panel, so all
 *     three RefreshModes deliberately collapse onto the same thing.
 *   - Do not set HWTCON_FLAG_CFA_SKIP at 32bpp. The driver then picks
 *     the wrong working buffer.
 *   - Do not write mdp_src_format. The panel is already RGBA.
 *
 * KOBOCHESS_WFM / _FLASH / _CFA / _DUMP override the choices below at
 * run time, which is how the waveform was bisected on real hardware
 * without a rebuild. See docs/PORTING.md.
 */

#ifndef LAST_MARKER
#define LAST_MARKER 0U
#endif

#ifndef DUMP_DIR
#define DUMP_DIR "/mnt/onboard/.adds/kobochess"
#endif

static struct {
    int fbfd;
    FBInkConfig cfg;
    FBInkState state;
    unsigned char *fb;
    size_t fb_size;
    unsigned int width;
    unsigned int height;
    unsigned int fb_stride;
    bool fb_bgra;
    bool fb_y8;
} g;

static WFM_MODE_INDEX_T g_wfm = WFM_GC16;
static CFA_MODE_INDEX_T g_cfa = CFA_DEFAULT;
static bool g_flash = true;
static bool g_dump;

static void refresh_mode_init(void)
{
    const char *wfm = getenv("KOBOCHESS_WFM");
    const char *flash = getenv("KOBOCHESS_FLASH");
    const char *cfa = getenv("KOBOCHESS_CFA");
    const char *dump = getenv("KOBOCHESS_DUMP");

    if (wfm != NULL) {
        if (strcmp(wfm, "gc16") == 0) {
            g_wfm = WFM_GC16;
        } else if (strcmp(wfm, "gcc16") == 0) {
            g_wfm = WFM_GCC16;
        } else if (strcmp(wfm, "glrc16") == 0) {
            g_wfm = WFM_GLRC16;
        } else if (strcmp(wfm, "gl16") == 0) {
            g_wfm = WFM_GL16;
        } else if (strcmp(wfm, "auto") == 0) {
            g_wfm = WFM_AUTO;
        }
    }

    if (flash != NULL) {
        g_flash = (atoi(flash) != 0);
    }
    if (cfa != NULL && strcmp(cfa, "g2") == 0) {
        g_cfa = CFA_G2;
    }
    if (dump != NULL) {
        g_dump = (atoi(dump) != 0);
    }

    fprintf(stderr, "kobo refresh: wfm=%u flash=%d cfa=%u dump=%d\n",
            (unsigned)g_wfm, (int)g_flash, (unsigned)g_cfa, (int)g_dump);
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

static void restore_nickel_fb(void)
{
    if (g.fbfd >= 0) {
        (void)fbink_reinit(g.fbfd, &g.cfg);
    }
}

static void copy_canvas_to_fb(const Canvas *canvas)
{
    unsigned int yy;

    for (yy = 0; yy < g.height; yy++) {
        unsigned char *dst = g.fb + ((size_t)yy * g.fb_stride);
        const unsigned char *src = canvas->pix + ((size_t)yy * canvas->stride);
        unsigned int xx;

        if (g.fb_y8) {
            for (xx = 0; xx < g.width; xx++) {
                dst[xx] = src[0];
                src += 4;
            }
            continue;
        }

        if (!g.fb_bgra) {
            memcpy(dst, src, canvas->stride);
            continue;
        }

        for (xx = 0; xx < g.width; xx++) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
            dst += 4;
            src += 4;
        }
    }
}

/*
 * LAST_MARKER always resolves to the update this FBInk session just
 * sent, so the fence cannot be skipped by a stale marker.
 */
static void wait_settled(void)
{
    int rc;

    if (g.state.can_wait_for_submission) {
        rc = fbink_wait_for_submission(g.fbfd, LAST_MARKER);
        if (rc < 0) {
            fprintf(stderr, "wait_for_submission rc=%d\n", rc);
        }
    }

    rc = fbink_wait_for_complete(g.fbfd, LAST_MARKER);
    if (rc < 0) {
        fprintf(stderr, "wait_for_complete rc=%d\n", rc);
    }
}

/* Red channel of a 4-byte-per-pixel buffer as a PGM, for bug reports. */
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
 * done? A non-zero count means the driver is writing back over our
 * scanlines, which looks exactly like a drawing bug but is not one.
 */
static void verify_fb(const Canvas *canvas)
{
    unsigned int y;
    unsigned int bad = 0;

    if (g.fb_y8 || g.fb_bgra) {
        return;
    }

    for (y = 0; y < g.height; y++) {
        if (memcmp(g.fb + ((size_t)y * g.fb_stride),
                   canvas->pix + ((size_t)y * canvas->stride),
                   canvas->stride) != 0) {
            bad++;
        }
    }

    fprintf(stderr, "fb readback: %u/%u rows differ\n", bad, g.height);
}

const FBInkState *kobo_display_state(void)
{
    return &g.state;
}

int kobo_display_open(PlatformInfo *info)
{
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    size_t required;
    int rc;

    memset(&g, 0, sizeof(g));
    g.fbfd = -1;
    g.cfg.is_quiet = true;
    g.cfg.ignore_alpha = true;
    g.cfg.bg_color = BG_WHITE;

    refresh_mode_init();

    g.fbfd = fbink_open();
    if (g.fbfd < 0) {
        fprintf(stderr, "fbink_open() failed: %d\n", g.fbfd);
        return -1;
    }

    rc = fbink_init(g.fbfd, &g.cfg);
    if (rc < 0) {
        fprintf(stderr, "fbink_init() failed: %d\n", rc);
        fbink_close(g.fbfd);
        g.fbfd = -1;
        return -1;
    }

    fbink_get_state(&g.cfg, &g.state);

    g.width = g.state.screen_width;
    g.height = g.state.screen_height;
    g.fb_stride = g.state.scanline_stride;
    g.fb_y8 = (g.state.bpp == 8);
    g.fb_bgra = (g.state.pixel_format == FBINK_PXFMT_BGRA ||
                 g.state.pixel_format == FBINK_PXFMT_BGR32);

    if (g.state.bpp != 8 && g.state.bpp != 32) {
        fprintf(stderr, "Need an 8- or 32-bit framebuffer (got %u bpp)\n",
                g.state.bpp);
        goto fail;
    }

    g.fb = fbink_get_fb_pointer(g.fbfd, &g.fb_size);
    if (g.fb == NULL) {
        fprintf(stderr, "fbink_get_fb_pointer() failed\n");
        goto fail;
    }

    required = (size_t)g.fb_stride * g.height;
    if (g.fb_size < required) {
        fprintf(stderr, "Framebuffer too small: %zu < %zu\n",
                g.fb_size, required);
        goto fail;
    }

    memset(&vinfo, 0, sizeof(vinfo));
    memset(&finfo, 0, sizeof(finfo));
    fbink_get_fb_info(&vinfo, &finfo);
    fprintf(stderr, "Device: %s (%s)\n",
            g.state.device_name, g.state.device_codename);
    fprintf(stderr,
            "Screen: %ux%u stride %u bpp %u pixfmt %u y8=%d bgra=%d colour=%d mtk=%d\n",
            g.width, g.height, g.fb_stride, g.state.bpp, g.state.pixel_format,
            (int)g.fb_y8, (int)g.fb_bgra, (int)g.state.has_color_panel,
            (int)g.state.is_mtk);
    fprintf(stderr,
            "Native fb: %ux%u rotate %u line_len %u rgb %u/%u/%u smem %u\n",
            vinfo.xres, vinfo.yres, vinfo.rotate, finfo.line_length,
            vinfo.red.offset, vinfo.green.offset, vinfo.blue.offset,
            finfo.smem_len);
    fprintf(stderr, "can_wait_for_submission=%d unreliable_wait_for=%d\n",
            (int)g.state.can_wait_for_submission,
            (int)g.state.unreliable_wait_for);

    if (g.state.is_mtk) {
        hwtcon_cmd("night_mode 0");
        hwtcon_cmd("fiti_power 1");
    }

    info->width = g.width;
    info->height = g.height;
    return 0;

fail:
    restore_nickel_fb();
    fbink_close(g.fbfd);
    g.fbfd = -1;
    return -1;
}

void kobo_display_close(void)
{
    g.fb = NULL;

    if (g.fbfd >= 0) {
        restore_nickel_fb();
        fbink_close(g.fbfd);
        g.fbfd = -1;
    }
}

void kobo_display_present(const Canvas *canvas, RefreshMode mode)
{
    static bool dumped;
    FBInkConfig cfg = g.cfg;
    FBInkRect rect;
    int rc;

    /* All modes flash: see the header comment about Kaleido ghosting. */
    (void)mode;

    copy_canvas_to_fb(canvas);

    cfg.wfm_mode = g_wfm;
    cfg.is_flashing = g_flash;
    cfg.dithering_mode = HWD_PASSTHROUGH;
    cfg.cfa_mode = g_cfa;

    rect.left = 0;
    rect.top = 0;
    rect.width = (unsigned short)g.width;
    rect.height = (unsigned short)g.height;

    rc = fbink_refresh_rect(g.fbfd, &rect, &cfg);
    if (rc < 0) {
        fprintf(stderr, "fbink_refresh_rect rc=%d\n", rc);
    }

    wait_settled();

    if (g_dump && !dumped) {
        dumped = true;
        verify_fb(canvas);
        dump_pgm(DUMP_DIR "/dump_canvas.pgm", canvas->pix,
                 canvas->width, canvas->height, canvas->stride);
        dump_pgm(DUMP_DIR "/dump_fb.pgm", g.fb,
                 g.width, g.height, g.fb_stride);
    }
}

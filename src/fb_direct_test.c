#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "fbink.h"

#define LOG_PATH "/mnt/onboard/.adds/kobochess/fb_direct_test.log"

int main(void)
{
    FILE *log = fopen(LOG_PATH, "w");

    if (log == NULL) {
        return 1;
    }

    fprintf(log, "Starting FB direct framebuffer test...\n");
    fflush(log);

    FBInkConfig cfg = {0};
    FBInkState state = {0};

    /*
     * Open framebuffer.
     */
    int fbfd = fbink_open();

    if (fbfd < 0) {
        fprintf(log, "fbink_open() failed: %d\n", fbfd);
        fclose(log);
        return 1;
    }

    fprintf(log, "Framebuffer opened: fd=%d\n", fbfd);

    /*
     * Initialize FBInk.
     */
    int rc = fbink_init(fbfd, &cfg);

    if (rc < 0) {
        fprintf(log, "fbink_init() failed: %d\n", rc);
        fbink_close(fbfd);
        fclose(log);
        return 1;
    }

    fprintf(log, "FBInk initialized successfully.\n");

    /*
     * Get framebuffer state.
     */
    fbink_get_state(&cfg, &state);

    fprintf(log, "Device: %s\n", state.device_name);
    fprintf(log, "Codename: %s\n", state.device_codename);
    fprintf(log, "Platform: %s\n", state.device_platform);

    fprintf(log,
            "Screen: %u x %u\n",
            state.screen_width,
            state.screen_height);

    fprintf(log,
            "View: %u x %u\n",
            state.view_width,
            state.view_height);

    fprintf(log,
            "Stride: %u bytes\n",
            state.scanline_stride);

    fprintf(log,
            "BPP: %u\n",
            state.bpp);

    fprintf(log,
            "Pixel format: %u\n",
            state.pixel_format);

    fprintf(log,
            "Current rotation: %u\n",
            state.current_rota);

    fprintf(log,
            "Native boot rotation: %u\n",
            state.ntx_boot_rota);

    fprintf(log,
            "NTX rotation quirk: %d\n",
            state.ntx_rota_quirk);

    fflush(log);

    /*
     * Get direct framebuffer pointer.
     */
    size_t buffer_size = 0;

    unsigned char *fb =
        fbink_get_fb_pointer(fbfd, &buffer_size);

    if (fb == NULL) {
        fprintf(log,
                "fbink_get_fb_pointer() failed.\n");

        fbink_close(fbfd);
        fclose(log);
        return 1;
    }

    fprintf(log,
            "Framebuffer pointer: %p\n",
            (void *)fb);

    fprintf(log,
            "Framebuffer size: %zu bytes\n",
            buffer_size);

    /*
     * Our Kobo is using 32-bit RGBA.
     */
    if (state.bpp != 32 ||
        state.pixel_format != FBINK_PXFMT_RGBA) {

        fprintf(log,
                "ERROR: Expected 32-bit RGBA framebuffer.\n");

        fprintf(log,
                "Actual BPP: %u\n",
                state.bpp);

        fprintf(log,
                "Actual pixel format: %u\n",
                state.pixel_format);

        fbink_close(fbfd);
        fclose(log);
        return 1;
    }

    /*
     * Verify framebuffer size.
     */
    size_t required_size =
        (size_t)state.scanline_stride *
        state.screen_height;

    if (buffer_size < required_size) {

        fprintf(log,
                "ERROR: framebuffer buffer is too small.\n");

        fprintf(log,
                "Required: %zu\n",
                required_size);

        fprintf(log,
                "Available: %zu\n",
                buffer_size);

        fbink_close(fbfd);
        fclose(log);
        return 1;
    }

    /*
     * Four black squares.
     *
     * Pixel format is RGBA:
     *
     *   R = 0
     *   G = 0
     *   B = 0
     *   A = 255
     */
    const unsigned int box = 150;

    const unsigned int screen_width =
        state.screen_width;

    const unsigned int screen_height =
        state.screen_height;

    fprintf(log,
            "Drawing four %ux%u black rectangles...\n",
            box,
            box);

    fflush(log);

    for (unsigned int y = 0;
         y < screen_height;
         y++) {

        unsigned char *row =
            fb + ((size_t)y * state.scanline_stride);

        for (unsigned int x = 0;
             x < screen_width;
             x++) {

            bool in_box =
                /*
                 * Top-left
                 */
                (x < box &&
                 y < box)

                ||

                /*
                 * Top-right
                 */
                (x >= screen_width - box &&
                 y < box)

                ||

                /*
                 * Bottom-left
                 */
                (x < box &&
                 y >= screen_height - box)

                ||

                /*
                 * Bottom-right
                 */
                (x >= screen_width - box &&
                 y >= screen_height - box);

            if (in_box) {

                /*
                 * Four bytes per pixel:
                 *
                 * [R][G][B][A]
                 */
                unsigned char *pixel =
                    row + ((size_t)x * 4);

                pixel[0] = 0x00;  /* R */
                pixel[1] = 0x00;  /* G */
                pixel[2] = 0x00;  /* B */
                pixel[3] = 0xFF;  /* A */
            }
        }
    }

    fprintf(log,
            "Framebuffer pixels written successfully.\n");

    fflush(log);

    /*
     * Refresh the entire framebuffer.
     */
    FBInkRect rect = {
        .left = 0,
        .top = 0,
        .width = (unsigned short)screen_width,
        .height = (unsigned short)screen_height
    };

    rc = fbink_refresh_rect(
        fbfd,
        &rect,
        &cfg
    );

    if (rc < 0) {

        fprintf(log,
                "fbink_refresh_rect() failed: %d\n",
                rc);

        fbink_close(fbfd);
        fclose(log);
        return 1;
    }

    fprintf(log,
            "Framebuffer refresh requested successfully.\n");

    fflush(log);

    /*
     * Keep the process alive so we can observe the result.
     */
    fprintf(log,
            "Keeping process alive for 10 seconds...\n");

    fflush(log);

    sleep(10);

    fprintf(log,
            "Test complete.\n");

    fclose(log);

    fbink_close(fbfd);

    return 0;
}

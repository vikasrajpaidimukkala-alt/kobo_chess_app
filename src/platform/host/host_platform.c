#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/*
 * Desktop backend. No e-ink, no evdev: every present() writes a PNG
 * and input comes from a script, so the UI can be developed and
 * regression-tested on a laptop.
 *
 * Environment:
 *   KOBOCHESS_OUT     directory for frame PNGs (default ".")
 *   KOBOCHESS_SIZE    "WIDTHxHEIGHT" (default 1264x1680, a Libra Colour)
 *   KOBOCHESS_SCRIPT  input script, one event per line:
 *                       tap <x> <y>
 *                       quit
 *                     Reaching the end of the script quits.
 *
 * Frames land as frame-0000.png, frame-0001.png and so on.
 */

#define MAX_EVENTS 256

static struct {
    unsigned int width;
    unsigned int height;
    const char *out_dir;
    unsigned int frame;
    InputEvent script[MAX_EVENTS];
    size_t script_len;
    size_t script_at;
} h;

static void parse_size(void)
{
    const char *spec = getenv("KOBOCHESS_SIZE");
    unsigned int w;
    unsigned int t;

    h.width = 1264;
    h.height = 1680;

    if (spec != NULL && sscanf(spec, "%ux%u", &w, &t) == 2 &&
        w >= 320 && t >= 320) {
        h.width = w;
        h.height = t;
    }
}

static void load_script(void)
{
    const char *path = getenv("KOBOCHESS_SCRIPT");
    char line[128];
    FILE *f;

    if (path == NULL) {
        return;
    }

    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "host: cannot read script %s\n", path);
        return;
    }

    while (h.script_len < MAX_EVENTS && fgets(line, sizeof(line), f)) {
        InputEvent *ev = &h.script[h.script_len];
        int x;
        int y;

        if (sscanf(line, " tap %d %d", &x, &y) == 2) {
            ev->kind = INPUT_TAP;
            ev->x = x;
            ev->y = y;
            h.script_len++;
        } else if (strncmp(line, "quit", 4) == 0) {
            ev->kind = INPUT_QUIT;
            ev->x = 0;
            ev->y = 0;
            h.script_len++;
        }
    }

    fclose(f);
    fprintf(stderr, "host: loaded %zu scripted events\n", h.script_len);
}

static void put_be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void write_chunk(FILE *f, const char *tag, const unsigned char *data,
                        size_t len)
{
    unsigned char head[8];
    unsigned char crcbuf[4];
    uLong crc;

    put_be32(head, (uint32_t)len);
    memcpy(head + 4, tag, 4);
    fwrite(head, 1, 8, f);
    if (len > 0) {
        fwrite(data, 1, len, f);
    }

    crc = crc32(0L, (const Bytef *)tag, 4);
    if (len > 0) {
        crc = crc32(crc, (const Bytef *)data, (uInt)len);
    }
    put_be32(crcbuf, (uint32_t)crc);
    fwrite(crcbuf, 1, 4, f);
}

static void write_png(const char *path, const Canvas *canvas)
{
    static const unsigned char SIG[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'
    };
    unsigned char ihdr[13];
    unsigned char *raw;
    unsigned char *comp;
    uLongf comp_len;
    size_t raw_len;
    unsigned int y;
    FILE *f;

    raw_len = ((size_t)canvas->width * 3 + 1) * canvas->height;
    raw = malloc(raw_len);
    if (raw == NULL) {
        return;
    }

    for (y = 0; y < canvas->height; y++) {
        const unsigned char *src = canvas->pix + ((size_t)y * canvas->stride);
        unsigned char *dst = raw + ((size_t)y * (canvas->width * 3 + 1));
        unsigned int x;

        *dst++ = 0; /* filter: none */
        for (x = 0; x < canvas->width; x++) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst += 3;
            src += 4;
        }
    }

    comp_len = compressBound((uLong)raw_len);
    comp = malloc(comp_len);
    if (comp == NULL) {
        free(raw);
        return;
    }

    if (compress2(comp, &comp_len, raw, (uLong)raw_len, 6) != Z_OK) {
        free(raw);
        free(comp);
        return;
    }

    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "host: cannot write %s\n", path);
        free(raw);
        free(comp);
        return;
    }

    put_be32(ihdr, canvas->width);
    put_be32(ihdr + 4, canvas->height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* colour type: truecolour */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    fwrite(SIG, 1, sizeof(SIG), f);
    write_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    write_chunk(f, "IDAT", comp, comp_len);
    write_chunk(f, "IEND", NULL, 0);
    fclose(f);

    free(raw);
    free(comp);
}

static int host_open(PlatformInfo *info)
{
    memset(&h, 0, sizeof(h));

    h.out_dir = getenv("KOBOCHESS_OUT");
    if (h.out_dir == NULL) {
        h.out_dir = ".";
    }

    parse_size();
    load_script();

    fprintf(stderr, "host: %ux%u, frames in %s\n",
            h.width, h.height, h.out_dir);

    info->width = h.width;
    info->height = h.height;
    return 0;
}

static void host_close(void)
{
    fprintf(stderr, "host: wrote %u frame(s)\n", h.frame);
}

static void host_present(const Canvas *canvas, RefreshMode mode)
{
    char path[512];

    (void)mode;

    snprintf(path, sizeof(path), "%s/frame-%04u.png", h.out_dir, h.frame);
    write_png(path, canvas);
    h.frame++;
}

static int host_poll(InputEvent *ev, int timeout_ms)
{
    (void)timeout_ms;

    if (h.script_at >= h.script_len) {
        ev->kind = INPUT_QUIT;
        ev->x = 0;
        ev->y = 0;
        return 1;
    }

    *ev = h.script[h.script_at++];
    return 1;
}

static void host_drain(void)
{
}

static const Platform HOST_PLATFORM = {
    .name = "host",
    .open = host_open,
    .close = host_close,
    .present = host_present,
    .poll = host_poll,
    .drain = host_drain
};

const Platform *platform_get(void)
{
    return &HOST_PLATFORM;
}

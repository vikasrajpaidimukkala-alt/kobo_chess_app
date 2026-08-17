#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "input.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_FDS 8

typedef struct {
    int fd;
    bool grab;
    bool touch;
    int abs_xmin;
    int abs_xmax;
    int abs_ymin;
    int abs_ymax;
} InDev;

static InDev g_devs[MAX_FDS];
static int g_ndevs;
static bool g_swap;
static bool g_mirror_x;
static bool g_mirror_y;
static unsigned int g_sw;
static unsigned int g_sh;
static size_t g_ev_size;
static int g_logged;

static int g_raw_x;
static int g_raw_y;
static int g_have_x;
static int g_have_y;
static int g_armed = 1;
static int g_tap_x = -1;
static int g_tap_y = -1;

static int scale_axis(int v, int min, int max, int out_max)
{
    long n;

    if (max <= min) {
        return v;
    }

    n = ((long)(v - min) * out_max) / (max - min);
    if (n < 0) {
        n = 0;
    }
    if (n > out_max) {
        n = out_max;
    }

    return (int)n;
}

static int needs_scale(const InDev *dev)
{
    int span_x = dev->abs_xmax - dev->abs_xmin;
    int span_y = dev->abs_ymax - dev->abs_ymin;
    int longest = (int)(g_sw > g_sh ? g_sw : g_sh);

    /* Only remap ADC-style ranges (e.g. 0..4095), not panel pixels. */
    return span_x > longest + 64 || span_y > longest + 64;
}

static void translate(int raw_x, int raw_y, int *out_x, int *out_y,
                      const InDev *dev)
{
    int x;
    int y;
    int w = (int)g_sw - 1;
    int h = (int)g_sh - 1;

    if (needs_scale(dev)) {
        if (g_swap) {
            x = scale_axis(raw_y, dev->abs_ymin, dev->abs_ymax, w);
            y = scale_axis(raw_x, dev->abs_xmin, dev->abs_xmax, h);
        } else {
            x = scale_axis(raw_x, dev->abs_xmin, dev->abs_xmax, w);
            y = scale_axis(raw_y, dev->abs_ymin, dev->abs_ymax, h);
        }
    } else if (g_swap) {
        x = raw_y;
        y = raw_x;
    } else {
        x = raw_x;
        y = raw_y;
    }

    if (g_mirror_x) {
        x = w - x;
    }
    if (g_mirror_y) {
        y = h - y;
    }

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x > w) {
        x = w;
    }
    if (y > h) {
        y = h;
    }

    *out_x = x;
    *out_y = y;
}

static void read_abs_range(int fd, int code, int *min, int *max)
{
    struct input_absinfo info;

    memset(&info, 0, sizeof(info));
    if (ioctl(fd, EVIOCGABS(code), &info) == 0 && info.maximum > info.minimum) {
        *min = info.minimum;
        *max = info.maximum;
        return;
    }

    *min = 0;
    *max = 0;
}

static int already_have_path(const char *path)
{
    int i;
    char link[64];
    char resolved[64];

    for (i = 0; i < g_ndevs; i++) {
        snprintf(link, sizeof(link), "/proc/self/fd/%d", g_devs[i].fd);
        memset(resolved, 0, sizeof(resolved));
        if (readlink(link, resolved, sizeof(resolved) - 1) > 0 &&
            strcmp(resolved, path) == 0) {
            return 1;
        }
    }

    return 0;
}

static int add_fd(int fd, bool touch, bool grab)
{
    InDev *d;

    if (g_ndevs >= MAX_FDS || fd < 0) {
        if (fd >= 0) {
            close(fd);
        }
        return -1;
    }

    d = &g_devs[g_ndevs];
    memset(d, 0, sizeof(*d));
    d->fd = fd;
    d->touch = touch;
    d->grab = false;

    if (touch) {
        read_abs_range(fd, ABS_MT_POSITION_X, &d->abs_xmin, &d->abs_xmax);
        if (d->abs_xmax <= d->abs_xmin) {
            read_abs_range(fd, ABS_X, &d->abs_xmin, &d->abs_xmax);
        }
        read_abs_range(fd, ABS_MT_POSITION_Y, &d->abs_ymin, &d->abs_ymax);
        if (d->abs_ymax <= d->abs_ymin) {
            read_abs_range(fd, ABS_Y, &d->abs_ymin, &d->abs_ymax);
        }

        fprintf(stderr, "Touch abs X=%d..%d Y=%d..%d scale=%d\n",
                d->abs_xmin, d->abs_xmax, d->abs_ymin, d->abs_ymax,
                needs_scale(d));
    }

    if (grab) {
        if (ioctl(fd, EVIOCGRAB, 1) == 0) {
            d->grab = true;
        } else {
            fprintf(stderr, "EVIOCGRAB failed on fd %d: %s\n",
                    fd, strerror(errno));
        }
    }

    g_ndevs++;
    return 0;
}

static int evdev_has_bit(int fd, unsigned int type, unsigned int code)
{
    unsigned char bits[(KEY_MAX / 8) + 1];
    unsigned int nbits = (type == 0) ? EV_MAX : KEY_MAX;

    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGBIT(type, (nbits / 8) + 1), bits) < 0) {
        return 0;
    }

    return bits[code / 8] & (1u << (code % 8));
}

static void scan_event_nodes(void)
{
    int i;

    for (i = 0; i < 12; i++) {
        char path[64];
        int fd;
        int is_touch;
        int is_keys;

        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        if (already_have_path(path)) {
            continue;
        }

        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

        is_touch = evdev_has_bit(fd, 0, EV_ABS) &&
                   (evdev_has_bit(fd, EV_ABS, ABS_MT_POSITION_X) ||
                    evdev_has_bit(fd, EV_ABS, ABS_X));
        is_keys = evdev_has_bit(fd, 0, EV_KEY) && !is_touch;

        if (is_touch) {
            fprintf(stderr, "Touch node %s\n", path);
            add_fd(fd, true, true);
        } else if (is_keys) {
            fprintf(stderr, "Key node %s\n", path);
            add_fd(fd, false, false);
        } else {
            close(fd);
        }
    }
}

int input_init(const Display *d)
{
    size_t count = 0;
    size_t i;
    FBInkInputDevice *devices;
    uint8_t canonical;
    INPUT_DEVICE_TYPE_T match;

    g_ndevs = 0;
    g_ev_size = 0;
    g_armed = 1;
    g_sw = d->width;
    g_sh = d->height;

    /*
     * Libra Colour / Clara Colour (KOReader KoboMonza):
     *   touch_switch_xy = true
     *   touch_mirrored_x = false
     *   touch_mirrored_y = true
     */
    if (d->state.is_mtk && d->state.has_color_panel) {
        g_swap = true;
        g_mirror_x = false;
        g_mirror_y = true;
    } else {
        g_swap = d->state.touch_swap_axes;
        g_mirror_x = d->state.touch_mirror_x;
        g_mirror_y = d->state.touch_mirror_y;
    }

    canonical = fbink_rota_native_to_canonical(d->state.current_rota);
    switch (canonical) {
    case FB_ROTATE_CW:
        g_swap = !g_swap;
        g_mirror_y = !g_mirror_y;
        break;
    case FB_ROTATE_UD:
        g_mirror_x = !g_mirror_x;
        g_mirror_y = !g_mirror_y;
        break;
    case FB_ROTATE_CCW:
        g_swap = !g_swap;
        g_mirror_x = !g_mirror_x;
        break;
    default:
        break;
    }

    fprintf(stderr,
            "Touch map: swap=%d mirror_x=%d mirror_y=%d (native rota %u, canonical %u, mtk=%d)\n",
            (int)g_swap, (int)g_mirror_x, (int)g_mirror_y,
            d->state.current_rota, canonical, (int)d->state.is_mtk);

    match = INPUT_TOUCHSCREEN | INPUT_TABLET | INPUT_PAGINATION_BUTTONS;
    devices = fbink_input_scan(match, 0U, 0U, &count);
    if (devices != NULL) {
        for (i = 0; i < count; i++) {
            if (devices[i].fd < 0) {
                continue;
            }

            if (devices[i].matched &&
                (devices[i].type & (INPUT_TOUCHSCREEN | INPUT_TABLET))) {
                fprintf(stderr, "Touch: %s (%s)\n", devices[i].path, devices[i].name);
                add_fd(devices[i].fd, true, true);
                devices[i].fd = -1;
            } else if (devices[i].matched &&
                       (devices[i].type & INPUT_PAGINATION_BUTTONS)) {
                fprintf(stderr, "Buttons: %s (%s)\n", devices[i].path, devices[i].name);
                add_fd(devices[i].fd, false, false);
                devices[i].fd = -1;
            } else {
                close(devices[i].fd);
                devices[i].fd = -1;
            }
        }

        free(devices);
    }

    scan_event_nodes();

    if (g_ndevs == 0) {
        fprintf(stderr, "No input devices found\n");
        return -1;
    }

    return 0;
}

void input_close(void)
{
    int i;

    for (i = 0; i < g_ndevs; i++) {
        if (g_devs[i].fd < 0) {
            continue;
        }
        if (g_devs[i].grab) {
            ioctl(g_devs[i].fd, EVIOCGRAB, 0);
            g_devs[i].grab = false;
        }
        close(g_devs[i].fd);
        g_devs[i].fd = -1;
    }

    g_ndevs = 0;
}

static void emit_tap(int x, int y)
{
    if (!g_armed) {
        return;
    }

    g_tap_x = x;
    g_tap_y = y;
    g_armed = 0;
    fprintf(stderr, "tap -> %d,%d (raw %d,%d)\n", x, y, g_raw_x, g_raw_y);
}

static void handle_core(unsigned int type, unsigned int code, int value,
                        InDev *dev)
{
    if (g_logged < 24) {
        fprintf(stderr, "ev type=%u code=%u value=%d touch=%d\n",
                type, code, value, (int)dev->touch);
        g_logged++;
    }

    if (type == EV_KEY && !dev->touch) {
        if (value == 1 && code != KEY_POWER) {
            g_tap_x = -2;
        }
        return;
    }

    if (!dev->touch) {
        return;
    }

    if (type == EV_KEY) {
        if (code == BTN_TOUCH || code == BTN_TOOL_FINGER ||
            code == BTN_TOOL_PEN) {
            if (value == 0) {
                g_armed = 1;
            }
        }
        return;
    }

    if (type == EV_ABS) {
        switch (code) {
        case ABS_MT_POSITION_X:
        case ABS_X:
            g_raw_x = value;
            g_have_x = 1;
            break;
        case ABS_MT_POSITION_Y:
        case ABS_Y:
            g_raw_y = value;
            g_have_y = 1;
            break;
        case ABS_MT_TRACKING_ID:
            if (value < 0) {
                g_armed = 1;
            }
            break;
        default:
            break;
        }
        return;
    }

    if (type == EV_SYN && code == SYN_REPORT) {
        int x;
        int y;

        if (!g_have_x || !g_have_y) {
            return;
        }

        translate(g_raw_x, g_raw_y, &x, &y, dev);
        emit_tap(x, y);
    }
}

static size_t detect_ev_size(const unsigned char *buf, ssize_t got)
{
    if (got <= 0) {
        return sizeof(struct input_event);
    }

    /* MTK kernels often use 64-bit timestamps (24-byte events). */
    if (got % 24 == 0 && (got % 16 != 0 || got == 24 || got == 48)) {
        return 24;
    }
    if (got % (ssize_t)sizeof(struct input_event) == 0) {
        return sizeof(struct input_event);
    }
    if (got % 24 == 0) {
        return 24;
    }

    return sizeof(struct input_event);
}

static void parse_buffer(const unsigned char *buf, ssize_t got, InDev *dev)
{
    size_t off;
    size_t ev_size;

    if (g_ev_size == 0) {
        g_ev_size = detect_ev_size(buf, got);
        fprintf(stderr, "input_event size %zu (read %zd)\n", g_ev_size, got);
    }

    ev_size = g_ev_size;
    if (ev_size < 16) {
        ev_size = 16;
    }

    for (off = 0; off + ev_size <= (size_t)got; off += ev_size) {
        const unsigned char *p = buf + off;
        uint16_t type16;
        uint16_t code16;
        int32_t value;
        size_t base = (ev_size >= 24) ? 16 : 8;

        memcpy(&type16, p + base, sizeof(type16));
        memcpy(&code16, p + base + 2, sizeof(code16));
        memcpy(&value, p + base + 4, sizeof(value));

        handle_core(type16, code16, value, dev);
    }
}

/*
 * Throw away whatever piled up while the engine was searching, so a
 * tap aimed at the old position is not replayed against the new one.
 */
void input_drain(void)
{
    int i;

    for (i = 0; i < g_ndevs; i++) {
        unsigned char buf[512];

        while (read(g_devs[i].fd, buf, sizeof(buf)) > 0) {
            /* nodes are non-blocking; loop until EAGAIN */
        }
    }

    g_armed = 1;
    g_tap_x = -1;
}

int input_poll(InputEvent *ev, int timeout_ms)
{
    struct pollfd pfds[MAX_FDS];
    int i;
    int n;
    int rc;

    ev->kind = INP_NONE;
    ev->x = 0;
    ev->y = 0;
    g_tap_x = -1;

    n = 0;
    for (i = 0; i < g_ndevs; i++) {
        pfds[n].fd = g_devs[i].fd;
        pfds[n].events = POLLIN;
        pfds[n].revents = 0;
        n++;
    }

    rc = poll(pfds, (nfds_t)n, timeout_ms);
    if (rc < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if (rc == 0) {
        /* Missed lift events on MTK: re-arm after idle. */
        g_armed = 1;
        return 0;
    }

    for (i = 0; i < g_ndevs; i++) {
        unsigned char buf[512];
        ssize_t got;

        if (!(pfds[i].revents & POLLIN)) {
            continue;
        }

        got = read(g_devs[i].fd, buf, sizeof(buf));
        if (got <= 0) {
            continue;
        }

        parse_buffer(buf, got, &g_devs[i]);
    }

    if (g_tap_x == -2) {
        ev->kind = INP_EXIT_KEY;
        return 1;
    }
    if (g_tap_x >= 0) {
        ev->kind = INP_TAP;
        ev->x = g_tap_x;
        ev->y = g_tap_y;
        return 1;
    }

    return 0;
}

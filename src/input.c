#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "input.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
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

static int g_raw_x;
static int g_raw_y;
static int g_have_x;
static int g_have_y;
static int g_down;
static int g_start_valid;
static int g_start_x;
static int g_start_y;
static int g_cur_x;
static int g_cur_y;
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

static void translate(int raw_x, int raw_y, int *out_x, int *out_y,
                      const InDev *dev)
{
    int x;
    int y;
    int w = (int)g_sw - 1;
    int h = (int)g_sh - 1;

    if (g_swap) {
        x = scale_axis(raw_y, dev->abs_ymin, dev->abs_ymax, w);
        y = scale_axis(raw_x, dev->abs_xmin, dev->abs_xmax, h);
    } else {
        x = scale_axis(raw_x, dev->abs_xmin, dev->abs_xmax, w);
        y = scale_axis(raw_y, dev->abs_ymin, dev->abs_ymax, h);
    }

    if (g_mirror_x) {
        x = w - x;
    }
    if (g_mirror_y) {
        y = h - y;
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
        if (d->abs_xmax <= d->abs_xmin) {
            d->abs_xmin = 0;
            d->abs_xmax = (int)g_sw - 1;
        }
        if (d->abs_ymax <= d->abs_ymin) {
            d->abs_ymin = 0;
            d->abs_ymax = (int)g_sh - 1;
        }
    }

    if (grab) {
        if (ioctl(fd, EVIOCGRAB, 1) == 0) {
            d->grab = true;
        } else {
            fprintf(stderr, "EVIOCGRAB failed on fd %d: %s\n", fd, strerror(errno));
        }
    }

    g_ndevs++;
    return 0;
}

int input_init(const Display *d)
{
    size_t count = 0;
    size_t i;
    FBInkInputDevice *devices;
    uint8_t canonical;
    INPUT_DEVICE_TYPE_T match;

    g_ndevs = 0;
    g_sw = d->width;
    g_sh = d->height;
    g_swap = d->state.touch_swap_axes;
    g_mirror_x = d->state.touch_mirror_x;
    g_mirror_y = d->state.touch_mirror_y;

    /*
     * Apply canonical rotation on top of the panel quirks, same
     * approach as FBInk's utils/finger_trace.c.
     */
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
            "Touch map: swap=%d mirror_x=%d mirror_y=%d (native rota %u, canonical %u)\n",
            (int)g_swap, (int)g_mirror_x, (int)g_mirror_y,
            d->state.current_rota, canonical);

    match = INPUT_TOUCHSCREEN | INPUT_TABLET | INPUT_PAGINATION_BUTTONS;
    devices = fbink_input_scan(match, 0U, 0U, &count);
    if (devices == NULL) {
        fprintf(stderr, "fbink_input_scan() failed\n");
    } else {
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
                /* Do not grab: power/cover handling stays with the kernel. */
                add_fd(devices[i].fd, false, false);
                devices[i].fd = -1;
            } else {
                close(devices[i].fd);
                devices[i].fd = -1;
            }
        }

        free(devices);
    }

    if (g_ndevs == 0) {
        int fd;

        fprintf(stderr, "Trying /dev/input/event1 and event0\n");
        fd = open("/dev/input/event1", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            add_fd(fd, true, true);
        }
        fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            add_fd(fd, false, false);
        }
    }

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

static void handle_event(const struct input_event *ev, InDev *dev)
{
    if (ev->type == EV_KEY && !dev->touch) {
        if (ev->value == 1 && ev->code != KEY_POWER) {
            g_tap_x = -2; /* sentinel: exit key */
        }
        return;
    }

    if (!dev->touch) {
        return;
    }

    if (ev->type == EV_KEY) {
        if (ev->code == BTN_TOUCH || ev->code == BTN_TOOL_FINGER) {
            g_down = ev->value > 0;
            if (!g_down) {
                /* lift is finalized on SYN_REPORT */
            } else {
                g_start_valid = 0;
            }
        }
        return;
    }

    if (ev->type == EV_ABS) {
        switch (ev->code) {
        case ABS_MT_POSITION_X:
        case ABS_X:
            g_raw_x = ev->value;
            g_have_x = 1;
            break;
        case ABS_MT_POSITION_Y:
        case ABS_Y:
            g_raw_y = ev->value;
            g_have_y = 1;
            break;
        case ABS_MT_TRACKING_ID:
            g_down = (ev->value >= 0);
            if (g_down) {
                g_start_valid = 0;
            }
            break;
        case ABS_MT_PRESSURE:
        case ABS_PRESSURE:
            g_down = ev->value > 0;
            break;
        default:
            break;
        }
        return;
    }

    if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
        int x;
        int y;
        int dx;
        int dy;

        if (g_have_x && g_have_y) {
            translate(g_raw_x, g_raw_y, &x, &y, dev);
            g_cur_x = x;
            g_cur_y = y;
        } else {
            x = g_cur_x;
            y = g_cur_y;
        }

        if (g_down) {
            if (!g_start_valid) {
                g_start_x = x;
                g_start_y = y;
                g_start_valid = 1;
            }
            return;
        }

        if (!g_start_valid) {
            return;
        }

        dx = x - g_start_x;
        dy = y - g_start_y;
        if (dx < 0) {
            dx = -dx;
        }
        if (dy < 0) {
            dy = -dy;
        }
        if (dx < 48 && dy < 48) {
            g_tap_x = g_start_x;
            g_tap_y = g_start_y;
        }
        g_start_valid = 0;
    }
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
        return 0;
    }

    for (i = 0; i < g_ndevs; i++) {
        struct input_event buf[32];
        ssize_t got;

        if (!(pfds[i].revents & POLLIN)) {
            continue;
        }

        got = read(g_devs[i].fd, buf, sizeof(buf));
        if (got <= 0) {
            continue;
        }

        {
            int n_ev = (int)(got / (ssize_t)sizeof(struct input_event));
            int e;

            for (e = 0; e < n_ev; e++) {
                handle_event(&buf[e], &g_devs[i]);
            }
        }
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

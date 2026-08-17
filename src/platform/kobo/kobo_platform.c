#include "kobo.h"

#include <stdio.h>

/*
 * Assembles the two halves of the Kobo backend into the one struct the
 * rest of the program sees.
 */

static unsigned int g_width;
static unsigned int g_height;

static int kobo_open(PlatformInfo *info)
{
    if (kobo_display_open(info) != 0) {
        return -1;
    }

    g_width = info->width;
    g_height = info->height;

    if (kobo_input_open(g_width, g_height) != 0) {
        /*
         * Not fatal: the launcher's page-turn keys and SIGTERM still
         * get the player out, which matters more than touch.
         */
        fprintf(stderr, "Input init failed; quit via keys or SIGTERM.\n");
    }

    return 0;
}

static void kobo_close(void)
{
    kobo_input_close();
    kobo_display_close();
}

static const Platform KOBO_PLATFORM = {
    .name = "kobo",
    .open = kobo_open,
    .close = kobo_close,
    .present = kobo_display_present,
    .poll = kobo_input_poll,
    .drain = kobo_input_drain
};

const Platform *platform_get(void)
{
    return &KOBO_PLATFORM;
}

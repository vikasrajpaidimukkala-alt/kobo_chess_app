#ifndef KOBOCHESS_HWTCON_KOBO_H
#define KOBOCHESS_HWTCON_KOBO_H

#include <stdint.h>
#include <sys/ioctl.h>

#define HWTCON_FLAG_CFA_EINK_G2 0x00000600u

#define UPDATE_MODE_PARTIAL 0x0u
#define UPDATE_MODE_FULL    0x1u

#define HWTCON_WAVEFORM_MODE_GC16  2u
#define HWTCON_WAVEFORM_MODE_GCC16 10u

struct hwtcon_rect {
    uint32_t top;
    uint32_t left;
    uint32_t width;
    uint32_t height;
};

struct hwtcon_update_data {
    struct hwtcon_rect update_region;
    uint32_t waveform_mode;
    uint32_t update_mode;
    uint32_t update_marker;
    unsigned int flags;
    int dither_mode;
};

#define HWTCON_IOCTL_MAGIC_NUMBER 'F'
#define HWTCON_SEND_UPDATE \
    _IOW(HWTCON_IOCTL_MAGIC_NUMBER, 0x2E, struct hwtcon_update_data)
#define HWTCON_WAIT_FOR_UPDATE_COMPLETE \
    _IOWR(HWTCON_IOCTL_MAGIC_NUMBER, 0x2F, uint32_t)
#define HWTCON_WAIT_FOR_UPDATE_SUBMISSION \
    _IOW(HWTCON_IOCTL_MAGIC_NUMBER, 0x37, uint32_t)

#endif

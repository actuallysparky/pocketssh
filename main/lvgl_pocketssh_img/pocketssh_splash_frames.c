/* Predecoded native frames from the supplied T-Deck PocketSSH GIF. */
#include "lvgl.h"

enum {
    POCKETSSH_SPLASH_WIDTH = 320,
    POCKETSSH_SPLASH_HEIGHT = 240,
    POCKETSSH_SPLASH_FRAME_COUNT = 16,
    POCKETSSH_SPLASH_FRAME_BYTES = POCKETSSH_SPLASH_WIDTH * POCKETSSH_SPLASH_HEIGHT * 2,
};

extern const uint8_t pocketssh_splash_frames_blob_start[]
    asm("_binary_pocketssh_splash_frames_rgb565_start");

#define SPLASH_FRAME(index) { \
    .header.magic = LV_IMAGE_HEADER_MAGIC, \
    .header.cf = LV_COLOR_FORMAT_RGB565, \
    .header.flags = 0, \
    .header.w = POCKETSSH_SPLASH_WIDTH, \
    .header.h = POCKETSSH_SPLASH_HEIGHT, \
    .header.stride = POCKETSSH_SPLASH_WIDTH * 2, \
    .data_size = POCKETSSH_SPLASH_FRAME_BYTES, \
    .data = pocketssh_splash_frames_blob_start + ((index) * POCKETSSH_SPLASH_FRAME_BYTES), \
}

const lv_image_dsc_t pocketssh_splash_frames[POCKETSSH_SPLASH_FRAME_COUNT] = {
    SPLASH_FRAME(0), SPLASH_FRAME(1), SPLASH_FRAME(2), SPLASH_FRAME(3),
    SPLASH_FRAME(4), SPLASH_FRAME(5), SPLASH_FRAME(6), SPLASH_FRAME(7),
    SPLASH_FRAME(8), SPLASH_FRAME(9), SPLASH_FRAME(10), SPLASH_FRAME(11),
    SPLASH_FRAME(12), SPLASH_FRAME(13), SPLASH_FRAME(14), SPLASH_FRAME(15),
};

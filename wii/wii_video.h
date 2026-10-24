#ifndef SMB_WII_VIDEO_H
#define SMB_WII_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

bool wii_video_init(void);
void wii_video_present(const uint8_t *rgb_frame);
void wii_video_shutdown(void);

#endif

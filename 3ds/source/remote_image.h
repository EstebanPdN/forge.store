// Main-thread API. Network/decode work belongs to one lazy background worker.
#pragma once
#include <stdbool.h>

bool remote_image_request(const char *url);
// Separate cached shape variant; decoding/cropping/masking runs on the worker.
bool remote_image_request_rounded(const char *url, int side, int radius);
bool remote_image_dimensions(const char *url, int *w, int *h);
bool remote_image_ready(const char *url);
// Pure drawing: returns true for resident pixels, false for the readable fallback.
bool remote_image_draw(const char *url, float x, float y, float w, float h, float alpha);
bool remote_image_draw_rounded(const char *url, float x, float y, int side, int radius, float alpha);
// Consumes coalesced dimension changes. Reflow once when this returns true.
bool remote_image_changed(void);
// Once per frame, just AFTER C3D_FrameBegin(C3D_FRAME_SYNCDRAW), before draws.
void remote_image_pump(void);
// Release offscreen GPU images and queued requests; retain bounded disk cache.
// Same safe frame boundary as pump(). An already working request may finish.
void remote_image_trim(void);
// Shutdown only, before network and C3D teardown: stop requests the worker to end,
// exit collects it within the shutdown deadline.
void remote_image_stop(void);
void remote_image_exit(void);

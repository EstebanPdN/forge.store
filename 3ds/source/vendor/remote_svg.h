#ifndef FORGE_REMOTE_SVG_H
#define FORGE_REMOTE_SVG_H
#include <stdbool.h>
#include <stddef.h>
/* Bounded SVG subset, straight-alpha RGBA. Caller frees *rgba with free().
 * Fits within 376x240 without enlarging; failure resets outputs to NULL/0/0.
 * Include remote_svg.c exactly once (normally from remote_image.c).
 * Calls are serialized internally; a concurrent/reentrant call returns false. */
bool remote_svg_decode(const unsigned char *data, size_t size,
                       unsigned char **rgba, int *width, int *height);
#endif

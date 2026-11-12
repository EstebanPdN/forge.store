#pragma once
#include <stdbool.h>
// Worker-only, bounded native-resolution pixels. No GPU access or network.
bool reader_image_load(const char *url, unsigned char **rgba, int *w, int *h);
bool reader_image_bundled(const char *url);
void reader_image_save(const char *url, const unsigned char *rgba, int w, int h);
void reader_image_invalidate(const char *url);

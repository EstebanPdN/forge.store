#pragma once
// Worker-side bilinear cover crop + antialiased rounded alpha at native size.
// Returns owned RGBA memory, or NULL for invalid dimensions/allocation failure.
unsigned char *image_round_rgba(const unsigned char *source,int width,int height,int side,int radius);

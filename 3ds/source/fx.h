// Banner particles and the completion burst.
#pragma once
#include "gfx.h"

void fx_set(const char *preset);
void fx_burst(float x, float y, int n);
void fx_update(float dt);
void fx_draw(float ox, float alpha);
bool fx_active(void);

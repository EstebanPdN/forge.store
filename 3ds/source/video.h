#pragma once
#include "gfx.h"
// Main-thread API. Start accepts only paths registered as verified by media.
// Seek is asynchronous; the last texture stays visible until a new frame arrives.
bool video_start(const char *file);
void video_stop(void); // asynchronous; immediately hides playback
void video_pump(void); // call EVERY frame, including when inactive; reaps without waiting
bool video_stopping(void); // wait before requesting cache deletion
void video_exit(void); // final teardown: bounded join; retain buffers if the worker has not exited
void video_update(void);
bool video_active(void);
bool video_paused(void);
void video_pause(void);
void video_toggle_mute(void);
bool video_muted(void);
bool video_has_audio(void);
void video_seek(float seconds);
float video_time(void);
float video_duration(void);
bool video_bottom(void);
void video_draw(bool bottom,float x,float y,float w,float h,float alpha);
const char *video_error(void);

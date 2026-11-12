#pragma once
#include "gh_markup.h"
#include <stdint.h>
typedef struct {uint16_t node;uint32_t begin,end;float x,y;} GhFlowRun;
typedef struct {GhFlowRun *runs;unsigned count,capacity;bool truncated;} GhFlow;
typedef float (*GhFlowMeasure)(const char *text,size_t bytes,unsigned char style,void *context);
void gh_flow_reset(GhFlow *flow);
void gh_flow_free(GhFlow *flow);
// Appends one paragraph. Collapses whitespace, wraps across inline style/link
// boundaries, preserves UTF-8 offsets and centers each complete line as a unit.
float gh_flow_layout(GhFlow *flow,const GhDocument *doc,int begin,int end,float left,float width,float y,float line_height,bool center,GhFlowMeasure measure,void *context);

#pragma once
#include "gh_flow.h"
#define GH_TABLE_CELLS 256
// Bounded render plan; no file/network/GPU work. Reused only on document reflow.
typedef struct {float x,y,w,h;} GhTableCell;
typedef struct {unsigned node,first,count;float x,y,h;} GhTableNode;
typedef struct {GhTableCell cells[GH_TABLE_CELLS];GhTableNode nodes[GH_DOC_NODES];unsigned count,placements;bool truncated;} GhTables;
typedef void (*GhTableImageSize)(const GhNode*,float,float*,float*,void*);
// Returns the first node after the table and appends its cells and text runs.
int gh_table_layout(GhTables*,GhFlow*,GhDocument*,int,float,float,float,float*,GhFlowMeasure,GhTableImageSize,void*);

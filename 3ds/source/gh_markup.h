#pragma once
#include <stdbool.h>
#include <stddef.h>
#define GH_DOC_NODES 2048
#define GH_DOC_BYTES 200000

typedef enum { GH_TEXT, GH_HEADING, GH_CODE, GH_QUOTE, GH_TABLE, GH_IMAGE, GH_LINK, GH_RULE,
               GH_RELEASE, GH_ASSET, GH_RELEASE_END } GhKind;
typedef struct { GhKind kind; char *text, *url; int level; unsigned group; unsigned char style; unsigned short table,table_row,table_col,table_cols; bool table_header; unsigned char list_depth; } GhNode;
typedef struct { GhNode *nodes; int count; size_t bytes; bool truncated; } GhDocument;
bool gh_document_parse(GhDocument *doc, const char *source, const char *repo, const char *branch, bool dark);
void gh_document_free(GhDocument *doc);

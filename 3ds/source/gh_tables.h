#pragma once
#include <stdbool.h>
#include <stddef.h>
// Project Markdown pipe tables into the same structural markers emitted for
// HTML tables. Keeps byte styles and the document's existing size bound.
void gh_tables_project(char **plain,unsigned char **styles,size_t *length,bool *truncated,size_t limit);

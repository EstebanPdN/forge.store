#pragma once
#include "gh_markup.h"
#include <jansson.h>
bool gh_document_releases(GhDocument *doc, json_t *releases, const char *repo,
                          const char *branch, bool dark);

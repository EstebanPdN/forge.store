#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void http_cache_init(void);
typedef bool (*HttpCancelled)(void *context);
// Allocated payload, NUL terminated even for images. Fixed HTTPS URLs at callers.
// foreground: immediate validated disk hit; refresh: conditional ETag/date request.
char *http_cache_get(const char *url, const char *accept, size_t limit, bool refresh, HttpCancelled cancelled,
                     void *context, size_t *length, char *error, size_t error_size);
void http_cache_path(const char *url, const char *accept, char *path, size_t size);
// Age since the last successful 200/304 response; never uses SD file mtimes.
bool http_cache_fresh(const char *url, const char *accept, uint32_t max_age);

void http_cache_invalidate(const char *url, const char *accept);

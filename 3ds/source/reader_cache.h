#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
char *reader_cache_load(const char *path,uint64_t now,uint64_t ttl,size_t limit,size_t *length);
bool reader_cache_save(const char *path,uint64_t now,const char *body,size_t length,size_t limit);

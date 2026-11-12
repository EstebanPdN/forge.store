#pragma once
#include <jansson.h>
// Worker-safe conversion of the same two public documents used by universal.js.
// Returned array owns all strings; callers own one reference. NULL on bad input.
json_t *universal_convert(json_t *full,json_t *store);

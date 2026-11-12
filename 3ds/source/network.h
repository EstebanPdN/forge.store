#pragma once
#include "common.h"
#include <curl/curl.h>
void network_options(CURL *curl);
void network_error(char *out, size_t size, const char *error);
// A completed request is transferred to the caller; only one reader request at a time.
void reader_request(const char *path, bool raw);
bool reader_busy(void);
char *reader_take(char *error, size_t size);
void reader_stop(void); // shutdown: cancel every request, without waiting
void reader_exit(void);

char *reader_plaintext(const char *source);

// Independent bounded requests: metadata/README/releases/commits cannot block each other.
#define READER_SLOTS 5
bool reader_slot_busy(int slot);
bool reader_request_slot(int slot, const char *path, bool raw);
// Slot 4 belongs to the live curated catalog; viewer slots remain independent.
bool reader_request_slot_ttl(int slot,const char *path,bool raw,u32 ttl);
char *reader_take_slot(int slot, char *error, size_t size);
void reader_cancel_slot(int slot);

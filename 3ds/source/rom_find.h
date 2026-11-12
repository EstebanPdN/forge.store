// Finds a user file (ROM, data built from it) the way the port itself does: any file name
// the port accepts, recognised by size, header bytes or CRC-32. The rules are the
// requirement's "detect" array in the catalog; each entry is one alternative:
//   dir     folder on the SD card ("/3ds/Starwing/")
//   names   exact file names, or
//   ext     extensions without the dot (any name); both ignore case, like the SD card
//   skip    names the port ignores; names starting with "." are always ignored
//   sizes / minSize   accepted file sizes
//   magic   [{at, any:[text...]} or {at, hex:[bytes...]}]  header bytes
//   crc32   {any:[hex...], tail:N}  CRC-32 of the last N bytes (whole file without tail)
//   derived true when the file is data the port built: it starts without the ROM
#pragma once
#include <jansson.h>
#include <stdbool.h>
#include <stddef.h>

/** True when the requirement has rules: the fixed "path" is then only where to copy it. */
bool rom_find_ruled(json_t *requirement);
/** Background IO only. First file that satisfies a rule; `path` receives where it is. */
bool rom_find_scan(json_t *requirement, char *path, size_t size, bool *derived);

// ---- cached answers for drawing code (main thread; the SD is read on a worker) ---------------
void rom_find_reset(void);
void rom_find_watch(json_t *requirement);
/** Asks for a new scan; the answer is in once rom_find_completed() reaches the ticket. */
unsigned rom_find_refresh(void);
unsigned rom_find_completed(void);
void rom_find_pump(float now);
/** False until the first scan ends. */
bool rom_find_known(json_t *requirement);
bool rom_find_have(json_t *requirement);
/** The file that was found ("sdmc:/3ds/…/europe.gba"), or NULL. */
const char *rom_find_path(json_t *requirement, bool *derived);
void rom_find_stop(void);
void rom_find_exit(void);

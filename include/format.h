#pragma once

#include <stdbool.h>
#include "types.h"


#define FLAGS_DRY_RUN       (1u)
#define FLAGS_ERASE         (1u<<1)
#define FLAGS_SECURE_ERASE  (1u<<2)
#define FLAGS_FORCE_FAT32   (1u<<3)
#define FLAGS_PRINT_FS      (1u<<4)
#define FLAGS_VERBOSE       (1u<<5)



void setVerboseMode(const bool verbose);
u32 formatSd(const char *const path, const char *const label, const u32 flags, const u64 overrTotSec);

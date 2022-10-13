#pragma once

#include <stdbool.h>
#include "types.h"



void setVerboseMode(const bool verbose);
u32 formatSd(const char *const path, const char *const label, const bool forceSdxcFat32, const u64 overrTotSec);

#pragma once

#include "types.h"


union ArgFlags
{
	struct
	{
		u8 dryRun     : 1;
		u8 erase      : 1;
		u8 secErase   : 1;
		u8 forceFat32 : 1;
		u8 printFs    : 1;
		u8 verbose    : 1;
	};
	u8 allFlags;
};



void setVerboseMode(const bool verbose);
u32 formatSd(const char *const path, const char *const label, const ArgFlags flags, const u64 overrTotSec);

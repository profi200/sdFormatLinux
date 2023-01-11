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

typedef struct
{
	// TODO: union with exFAT vars.
	u64 totSec;
	u8  heads;
	u8  secPerTrk;
	u8  fatBits;
	u32 alignment;
	u32 secPerClus;
	u32 rsvdSecCnt;
	u32 secPerFat;
	u32 fsAreaSize;
	u32 partStart;
	u32 maxClus;
} FormatParams;



u32 formatSd(const char *const path, const char *const label, const ArgFlags flags, const u64 overrTotSec);

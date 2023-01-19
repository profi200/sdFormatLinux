#pragma once

#include <string>
#include "types.h"


// The smallest card we can format without running into issues is 64 KiB.
#define MIN_CAPACITY        (1024u * 64 / 512)
#define MAX_CAPACITY_FAT32  (0xFFFFFFFFu)


union ArgFlags
{
	struct
	{
		u8 bigClusters : 1;
		//u8 dryRun      : 1;
		u8 erase       : 1;
		u8 forceFat32  : 1;
		u8 secErase    : 1;
		u8 verbose     : 1;
	};
	u8 allFlags;
};

// Note: Unless specified otherwise everything is in logical sectors.
typedef struct
{
	// TODO: union with exFAT vars.
	u64 totSec;
	u16 bytesPerSec;
	u8  heads;
	u8  secPerTrk;
	u8  fatBits;
	u32 alignment;  // In logical sectors.
	u32 secPerClus;
	u32 rsvdSecCnt;
	u32 secPerFat;
	u32 fsAreaSize; // In logical sectors.
	u32 partStart;  // In logical sectors.
	u32 maxClus;    // Logical clusters.
} FormatParams;



u32 formatSd(const char *const path, const std::string &label, const ArgFlags flags, const u64 overrTotSec);

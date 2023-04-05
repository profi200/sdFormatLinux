#pragma once

// SPDX-License-Identifier: MIT

#include <string>
#include "types.h"


// The smallest card we can format without running into issues is 64 KiB.
#define MIN_CAPACITY        (1024u * 64 / 512)
#define MAX_CAPACITY_FAT32  (0xFFFFFFFFu)
#define PHY2LOG(x, bps)     ((x) / ((bps)>>9)) // Physical to logical sectors.
#define LOG2PHY(x, bps)     ((x) * ((bps)>>9)) // Logical to physical sectors.


union ArgFlags
{
	struct
	{
		u8 bigClusters : 1;
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
	u64 totSec;
	u32 alignment;          // In logical sectors.
	u32 secPerClus;
	union
	{
		struct // FAT12/16/32
		{
			u32 rsvdSecCnt;
			u32 secPerFat;
			u32 fsAreaSize; // In logical sectors.
			u32 partStart;  // In logical sectors.
			u32 maxClus;    // Logical clusters.
		};
		struct // exFAT
		{
			u64 partitionOffset;   // In logical sectors.
			u64 volumeLength;      
			u32 fatOffset;         // In logical sectors.
			u32 fatLength;
			u32 clusterHeapOffset; // In logical sectors.
			u32 clusterCount;
		};
	};
	u16 bytesPerSec;
	u8  fatBits;
	u8  heads;
	u8  secPerTrk;
} FormatParams;



u32 formatSd(const char *const path, const std::string &label, const ArgFlags flags, const u64 overrTotSec);

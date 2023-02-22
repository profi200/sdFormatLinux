// SPDX-License-Identifier: MIT

#include <memory>
#include <cstdio>
#include <cstring>
#include "types.h"
#include "format.h"
#include "mbr.h"
#include "exfat.h"
#include "fat.h"
#include "errors.h"
#include "buffered_fs_writer.h"
#include "verbose_printf.h"
#include "privileges.h"


typedef struct
{
	u16 cap; // Capacity in MiB.
	u8  heads;
	u8  secPerTrk;
} GeometryData;

typedef struct
{
	u8  capLog2; // log2(capacity in sectors).
	u8  fatBits;
	u16 secPerClus;
	u32 alignment;
} AlignData;



// TODO: fatBits is determined from SC and TS.
static bool getFormatParams(const u64 totSec, const ArgFlags flags, FormatParams &params)
{
	if(totSec == 0) return false;
	if(flags.forceFat32 && totSec > MAX_CAPACITY_FAT32) return false;

	static const GeometryData geometryTable[10] =
	{
		{   2,   2, 16}, // <= 2     MiB.
		{  16,   2, 32}, // <= 16    MiB.
		{  32,   4, 32}, // <= 32    MiB.
		{ 128,   8, 32}, // <= 128   MiB.
		{ 256,  16, 32}, // <= 256   MiB.
		{ 504,  16, 63}, // <= 504   MiB.
		{1008,  32, 63}, // <= 1008  MiB.
		{2016,  64, 63}, // <= 2016  MiB.
		{4032, 128, 63}, // <= 4032  MiB.
		{   0, 255, 63}  // Everything higher.
	};

	static const AlignData alignTable[10] =
	{
		{14, 12,   16,     16}, // <=8   MiB.
		{17, 12,   32,     32}, // <=64  MiB.
		{19, 16,   32,     64}, // <=256 MiB.
		{21, 16,   32,    128}, // <=1   GiB.
		{22, 16,   64,    128}, // <=2   GiB.
		{26, 32,   64,   8192}, // <=32  GiB.
		{28, 64,  256,  32768}, // <=128 GiB.
		{30, 64,  512,  65536}, // <=512 GiB.
		{32, 64, 1024, 131072}, // <=2   TiB.
		{ 0,  0,    0,      0}  // Higher is not supported (yet).
	};

	const GeometryData *geometryData = geometryTable;
	while(geometryData->cap != 0 && totSec>>11 > geometryData->cap) geometryData++;
	params.heads     = geometryData->heads;
	params.secPerTrk = geometryData->secPerTrk;

	const AlignData *alignParams = alignTable;
	while(alignParams->capLog2 != 0 && totSec > 1ull<<alignParams->capLog2) alignParams++;
	const u8 capLog2 = alignParams->capLog2;
	if(capLog2 == 0)
	{
		fputs("Error: SD card capacity not supported.\n", stderr);
		return false;
	}

	u16 bytesPerSec = 512;
	u8  fatBits     = alignParams->fatBits;
	u32 secPerClus  = alignParams->secPerClus;
	if(flags.forceFat32 && fatBits > 32)
	{
		fatBits = 32;

		if(flags.bigClusters)
		{
			// Increase logical sector size to work around the sectors per cluster limitation for FAT32.
			if(capLog2 == 28)      bytesPerSec = 1024; // <=128 GiB.
			else if(capLog2 == 30) bytesPerSec = 2048; // <=512 GiB.
			else                   bytesPerSec = 4096; // <=2   TiB.
			secPerClus = 128;
		}
		else if(secPerClus > 128)
		{
			secPerClus = 128;
			fputs("Warning: FAT32 doesn't support clusters bigger than 64 KiB. Overriding.\n", stderr);
		}
	}
	params.totSec      = totSec / (bytesPerSec>>9);
	params.bytesPerSec = bytesPerSec;
	params.fatBits     = fatBits;
	params.alignment   = alignParams->alignment / (bytesPerSec>>9);
	params.secPerClus  = secPerClus;

	if(fatBits <= 16)      calcFormatFat(params);
	else if(fatBits == 32) calcFormatFat32(params);
	else                   calcFormatExFat(params);

	if(fatBits <= 32)
	{
		if(params.rsvdSecCnt > 0xFFFF)
		{
			fputs("Error: Reserved sector count overflowed. Can't format the SD card with these parameters.\n", stderr);
			return false;
		}

		const u32 maxClus = params.maxClus;
		if(params.secPerFat * bytesPerSec / (fatBits / 8) < maxClus)
		{
			fputs("Error: FAT doesn't contain enough entries to allocate all clusters.\n", stderr);
			return false;
		}

		const u32 calcFsArea = params.rsvdSecCnt + (2 * params.secPerFat) +
		                       ((32 * (fatBits < 32 ? 512 : 0) + bytesPerSec - 1) / bytesPerSec);
		if(params.fsAreaSize != calcFsArea)
		{
			fputs("Error: Filesystem area smaller than reserved sectors + FATs.\n", stderr);
			return false;
		}

		/*if(params.fsAreaSize > params.alignment)
			fputs("Warning: Filesystem area overlaps with data area. May reduce performance and lifetime.\n", stderr);*/

		// fatgen103.doc: Less than 4085 is FAT12. Less than 65525 is FAT16. Otherwise FAT32.
		// mkfs.fat:      Up to 4084 is FAT12. 4087-65524 is FAT16. 65525-268435444 is FAT32.
		// (Win) fastfat.sys, (Linux) msdos.ko/vfat.ko detect FAT32 when fatSz16 is set to zero.
		// Note: mkfs uses different values because of many FAT drivers with off by X bugs.
		const u32 upperBound = 0x0FFFFFF4u & (0xFFFFFFFFu>>(32 - fatBits)); // 0xFF4, 0xFFF4 and 0x0FFFFFF4.
		if((fatBits == 12 && maxClus > 4084u) || (fatBits == 16 && maxClus < 4087u) ||
		   (fatBits == 32 && maxClus < 65525u) || maxClus > upperBound)
		{
			fputs("Error: Too few/many clusters for FAT variant.\n", stderr);
			return false;
		}
	}
	// TODO: exFAT checks.

	return true;
}

static void printFormatParams(const FormatParams &params)
{
	const char *fsName;
	if(params.fatBits == 12)      fsName = "FAT12";
	else if(params.fatBits == 16) fsName = "FAT16";
	else if(params.fatBits == 32) fsName = "FAT32";
	else                          fsName = "exFAT";

	printf("Filesystem type:      %s\n", fsName);
	printf("Bytes per sector:     %" PRIu16 "\n", params.bytesPerSec);
	printf("Heads:                %" PRIu8 "\n", params.heads);
	printf("Sectors per track:    %" PRIu8 "\n", params.secPerTrk);
	printf("Alignment:            %" PRIu32 "\n", params.alignment);
	printf("Reserved sectors:     %" PRIu32 "\n", params.rsvdSecCnt);
	printf("Sectors per cluster:  %" PRIu32 "\n", params.secPerClus);
	printf("Sectors per FAT:      %" PRIu32 "\n", params.secPerFat);
	printf("Filesystem area size: %" PRIu32 "\n", params.fsAreaSize);
	printf("Partition start:      %" PRIu32 "\n", params.partStart);
	printf("Maximum clusters:     %" PRIu32 "\n", params.maxClus);
}

u32 formatSd(const char *const path, const std::string &label, const ArgFlags flags, const u64 overrTotSec)
{
	BufferedFsWriter dev;
	if(dev.open(path) != 0) return ERR_DEV_OPEN;
	dropPrivileges();

	u64 totSec = dev.getSectors();
	if(totSec < MIN_CAPACITY)
	{
		fputs("SD card capacity too small.\n", stderr);
		return ERR_DEV_TOO_SMALL;
	}

	// Allow overriding the capacity only if the new capacity is lower.
	if(overrTotSec >= MIN_CAPACITY && overrTotSec < totSec)
		totSec = overrTotSec;
	printf("SD card contains %" PRIu64 " sectors.\n", totSec);

	if(flags.erase || flags.secErase)
	{
		verbosePuts("Erasing SD card...");

		// Note: Linux doesn't support secure erase even if it's technically
		//       possible by password locking the card and then forcing erase.
		const int eraseRes = dev.eraseAll(flags.secErase);
		if(eraseRes == EOPNOTSUPP)
		{
			fputs("SD card erase not supported. Ignoring.\n", stderr);
		}
		else if(eraseRes != 0) return ERR_ERASE;
	}

	// Collect and calculate all the infos needed for formatting.
	FormatParams params{};
	if(!getFormatParams(totSec, flags, params))
	{
		fputs("The SD card can not be formatted with the given parameters.\n", stderr);
		return ERR_FORMAT_PARAMS;
	}

	// Create a new Master Boot Record and partition.
	verbosePuts("Creating new partition table and partition...");
	if(createMbrAndPartition(params, dev) != 0) return ERR_PARTITION;

	// Clear filesystem areas and write a new Volume Boot Record.
	// TODO: Label should be upper case and some chars are not allowed. Implement conversion + checks.
	//       mkfs.fat allows lower case but warns about it.
	verbosePuts("Formatting the partition...");
	if(params.fatBits <= 32)
	{
		if(makeFsFat(params, dev, label) != 0) return ERR_FORMAT;
	}
	else
	{
		// TODO: exFAT.
	}

	// Explicitly close dev to get the result.
	if(dev.close() != 0) return ERR_CLOSE_DEV;

	puts("Successfully formatted the card.");
	printFormatParams(params);

	return 0;
}

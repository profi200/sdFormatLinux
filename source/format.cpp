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



// TODO: fatBits is determined from SC and TS.
static bool getFormatParams(const u64 totSec, const bool forceSdxcFat32, FormatParams &paramsOut)
{
	if(totSec == 0) return false;
	if(totSec >= 1ull<<32 && forceSdxcFat32) return false;

	typedef struct
	{
		u16 cap; // Capacity in MiB.
		u8  heads;
		u8  secPerTrk;
	} GeometryData;
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

	typedef struct
	{
		u8  capLog2; // log2(capacity in sectors).
		u8  fatBits;
		u16 secPerClus;
		u32 alignment;
	} AlignData;
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

	paramsOut.totSec = totSec;

	const GeometryData *geometryData = geometryTable;
	while(geometryData->cap != 0 && totSec>>11 > geometryData->cap) geometryData++;
	paramsOut.heads     = geometryData->heads;
	paramsOut.secPerTrk = geometryData->secPerTrk;

	const AlignData *alignParams = alignTable;
	while(alignParams->capLog2 != 0 && totSec > 1ull<<alignParams->capLog2) alignParams++;
	if(alignParams->capLog2 == 0)
	{
		fputs("Error: SD card capacity not supported.", stderr);
		return false;
	}

	const u8 fatBits = (alignParams->capLog2 > 26 && forceSdxcFat32 ? 32 : alignParams->fatBits);
	paramsOut.fatBits    = fatBits;
	paramsOut.alignment  = alignParams->alignment;
	paramsOut.secPerClus = alignParams->secPerClus;

	if(fatBits <= 16)      calcFormatFat((u32)totSec, paramsOut);
	else if(fatBits == 32) calcFormatFat32((u32)totSec, paramsOut);
	else                   calcFormatExFat(/*totSec, *paramsOut*/); // TODO

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

u32 formatSd(const char *const path, const char *const label, const ArgFlags flags, const u64 overrTotSec)
{
	BufferedFsWriter dev;
	if(dev.open(path) != 0) return ERR_DEV_OPEN;
	dropPrivileges();

	// The smallest card we can format without running into issues is 64 KiB.
	// TODO: Also test with non-pow2 sizes.
	u64 totSec = dev.getSectors();
	if(totSec < (1024 * 64 / 512))
	{
		fputs("SD card capacity too small.\n", stderr);
		return ERR_DEV_TOO_SMALL;
	}

	// Allow overriding the capacity only if the new capacity is lower.
	if(overrTotSec >= (1024 * 64 / 512) && overrTotSec < totSec)
		totSec = overrTotSec;
	verbosePrintf("SD card contains %" PRIu64 " sectors.\n", totSec);

	if(flags.erase || flags.secErase)
	{
		verbosePuts("Erasing SD card...");

		// Note: Linux doesn't support secure erase even if it's technically
		//       possible by password locking the card and then forcing erase.
		const int discardRes = dev.discardAll(flags.secErase);
		if(discardRes == EOPNOTSUPP)
		{
			fputs("SD card can't be erased. Ignoring.\n", stderr);
		}
		else if(discardRes != 0) return ERR_ERASE;
	}

	FormatParams params{};
	getFormatParams(totSec, flags.forceFat32, params);

	// Create a new Master Boot Record and partition.
	verbosePuts("Creating new partition table and partition...");
	if(createMbrAndPartition(&params, dev) != 0) return ERR_PARTITION;

	// Clear filesystem areas and write a new Volume Boot Record.
	// TODO: Label should be upper case and some chars are not allowed. Implement conversion + checks.
	//       mkfs.fat allows lower case but warns about it.
	verbosePuts("Formatting the partition...");
	// TODO: Depending on FS type call the format function here.
	if(makeFsFat(params, dev, label) != 0) return ERR_FORMAT;

	// Explicitly close dev to get the result.
	if(dev.close() != 0) return ERR_CLOSE_DEV;

	puts("Successfully formatted the card.");
	printFormatParams(params);

	return 0;
}

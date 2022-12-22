#include <memory>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/random.h> // getrandom()...
#include "types.h"
#include "format.h"
#include "exfat.h"
#include "fat.h"
#include "blockdev.h"


typedef struct
{
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

static bool g_verbose = false;



void setVerboseMode(const bool verbose)
{
	g_verbose = verbose;
}

static int verbosePuts(const char *str)
{
	int res = 0;
	if(g_verbose == true) res = puts(str);
	return res;
}

static int verbosePrintf(const char *format, ...)
{
	int res = 0;

	va_list args;
	va_start(args, format);

	if(g_verbose) res = vprintf(format, args);

	va_end(args);

	return res;
}

// FAT12/FAT16.
static void calcFormatFat(const u32 totSec, FormatParams *const params)
{
	const u32 fatBits     = params->fatBits;
	const u32 alignment   = params->alignment;
	const u32 secPerClus  = params->secPerClus;
	const u32 bytesPerSec = 512;
	const u32 rootEntCnt  = 512;
	const u32 rsvdSecCnt  = 1;
	u32 secPerFat         = ceil((double)(totSec / secPerClus * fatBits) / (bytesPerSec * 8));
	u32 fsAreaSize;
	u32 partStart;
	u32 maxClus;
	while(1)
	{
		fsAreaSize = rsvdSecCnt + 2 * secPerFat + ceil((double)(32 * rootEntCnt) / bytesPerSec);
		partStart  = alignment - fsAreaSize % alignment;
		if(partStart != alignment) partStart += alignment;

		u32 tmpSecPerFat;
		while(1)
		{
			maxClus      = (totSec - partStart - fsAreaSize) / secPerClus;
			tmpSecPerFat = ceil((double)((2 + maxClus) * fatBits) / (bytesPerSec * 8));

			if(tmpSecPerFat <= secPerFat) break;

			partStart += alignment;
		}

		if(tmpSecPerFat == secPerFat) break;

		secPerFat = tmpSecPerFat;
	}

	params->rsvdSecCnt = rsvdSecCnt;
	params->secPerFat  = secPerFat;
	params->fsAreaSize = fsAreaSize;
	params->partStart  = partStart;
	params->maxClus    = maxClus;
}

// TODO: Using u32 for totSec limits us to <2 TiB maximum capacity.
static void calcFormatFat32(const u32 totSec, FormatParams *const params)
{
	const u32 fatBits     = 32;
	const u32 bytesPerSec = 512;
	const u32 alignment   = params->alignment;
	u32 secPerClus        = params->secPerClus;

	if(secPerClus > 128)
	{
		fputs("Warning: FAT32 doesn't support more than 64 KiB per cluster."
		      " Using 64 which might lower performance and lifetime.\n", stderr);
		params->secPerClus = 128;
		secPerClus         = 128;
	}

	u32 secPerFat       = ceil((double)(totSec / secPerClus * fatBits) / (bytesPerSec * 8));
	const u32 partStart = alignment;
	u32 rsvdSecCnt;
	u32 fsAreaSize;
	u32 maxClus;
	while(1)
	{
		rsvdSecCnt = alignment - (2 * secPerFat) % alignment;
		if(rsvdSecCnt < 9) rsvdSecCnt += alignment;
		fsAreaSize = rsvdSecCnt + 2 * secPerFat;
		u32 tmpSecPerFat;
		while(1)
		{
			maxClus      = (totSec - partStart - fsAreaSize) / secPerClus;
			tmpSecPerFat = ceil((double)((2 + maxClus) * fatBits) / (bytesPerSec * 8));

			if(tmpSecPerFat <= secPerFat) break;

			fsAreaSize += alignment;
			rsvdSecCnt += alignment;
		}

		if(tmpSecPerFat == secPerFat) break;

		secPerFat--;
	}

	params->rsvdSecCnt = rsvdSecCnt;
	params->secPerFat  = secPerFat;
	params->fsAreaSize = fsAreaSize;
	params->partStart  = partStart;
	params->maxClus    = maxClus;
}

static void calcFormatExFat(void) // (const u64 totSec, FormatParams *const paramsOut)
{
	// TODO
	fputs("exFAT is not supported yet.\n", stderr);
}

// TODO: fatBits is determined from SC and TS.
static bool getFormatParams(const u64 totSec, const bool forceSdxcFat32, FormatParams *const paramsOut)
{
	if(totSec == 0) return false;
	if(totSec > 1ull<<32 && forceSdxcFat32) return false; // TODO: Check the exact limit. Also check for too small cards.

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

	paramsOut->totSec = totSec;

	const GeometryData *geometryData = geometryTable;
	while(geometryData->cap != 0 && totSec>>11 > geometryData->cap) geometryData++;
	paramsOut->heads     = geometryData->heads;
	paramsOut->secPerTrk = geometryData->secPerTrk;

	const AlignData *alignParams = alignTable;
	while(alignParams->capLog2 != 0 && totSec > 1ull<<alignParams->capLog2) alignParams++;
	if(alignParams->capLog2 == 0)
	{
		fputs("Error: SD card capacity not supported.", stderr);
		return false;
	}

	const u8 fatBits = (alignParams->capLog2 > 26 && forceSdxcFat32 ? 32 : alignParams->fatBits);
	paramsOut->fatBits    = fatBits;
	paramsOut->alignment  = alignParams->alignment;
	paramsOut->secPerClus = alignParams->secPerClus;

	if(fatBits <= 16)      calcFormatFat((u32)totSec, paramsOut);
	else if(fatBits == 32) calcFormatFat32((u32)totSec, paramsOut);
	else                   calcFormatExFat(/*totSec, paramsOut*/); // TODO

	return true;
}

static void printFormatParams(const FormatParams *const params)
{
	if(!g_verbose) return;

	const char *fsName;
	if(params->fatBits == 12)      fsName = "FAT12";
	else if(params->fatBits == 16) fsName = "FAT16";
	else if(params->fatBits == 32) fsName = "FAT32";
	else                           fsName = "exFAT";

	puts("Format parameters:");
	printf("\tFilesystem type:      %s\n", fsName);
	printf("\tHeads:                %" PRIu8 "\n", params->heads);
	printf("\tSectors per track:    %" PRIu8 "\n", params->secPerTrk);
	printf("\tAlignment:            %" PRIu32 "\n", params->alignment);
	printf("\tReserved sectors:     %" PRIu32 "\n", params->rsvdSecCnt);
	printf("\tSectors per cluster:  %" PRIu32 "\n", params->secPerClus);
	printf("\tSectors per FAT:      %" PRIu32 "\n", params->secPerFat);
	printf("\tFilesystem area size: %" PRIu32 "\n", params->fsAreaSize);
	printf("\tPartition start:      %" PRIu32 "\n", params->partStart);
	printf("\tMaximum clusters:     %" PRIu32 "\n", params->maxClus);
}

// TODO: This may not be the most accurate.
static u32 makeVolId(void)
{
	// tm_mon is 0-11 WHY?
	// tm_sec is 0-60 WHY?
	// I think we can get away pretending there are no milliseconds.
	const time_t _time = time(NULL);
	const struct tm *const now = localtime(&_time);
	const u16 lo = (u16)((now->tm_mon + 1)<<8 | now->tm_mday) + ((now->tm_sec % 60)<<8 /*| (ms / 10)*/);
	const u16 hi = (u16)(now->tm_hour<<8 | now->tm_min) + (now->tm_year + 1900);
	const u32 volId = (u32)hi<<16 | lo;

	//printf("year: %u, month: %u, day: %u, hour: %u, minute: %u, second: %u\n",
	//       now->tm_year, now->tm_mon, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);
	verbosePrintf("Volume ID: 0x%08" PRIX32 "\n", volId);
	return volId;
}

static u32 lba2chs(u64 lba, const u32 heads, const u32 secPerTrk)
{
	if(lba <= 16450560)
	{
		const u32 spc = heads * secPerTrk;

		const u32 c = lba / spc;
		lba %= spc;
		const u32 h = lba / secPerTrk;
		const u32 s = lba % secPerTrk + 1;

		//printf("lba2chs(): c: %" PRIu32 ", h: %" PRIu32 ", s: %" PRIu32 "\n", c, h, s);
		return (c & 0xFF)<<16 | (c & 0x300)<<6 | s<<8 | h;
	}

	// Return the maximum if lba is too big.
	// SDFormatter uses end head 254 for a 16 GB card. Bug?
	//puts("lba2chs(): c: 1023, h: 255, s: 63");
	return 0x00FFFFFF; // Cylinder 1023, head 255, sector 63.
}

static void makeMbrPartition(const FormatParams *const params, const bool bootable, Mbr *const mbr)
{
	// Master Boot Record (MBR).
	// TODO: If getrandom() returns -1 abort.
	while(getrandom(&mbr->diskId, 4, 0) != 4);
	verbosePrintf("Disk ID: 0x%08" PRIX32 "\n", mbr->diskId);

	mbr->partTable[0].bootable = (bootable ? 0x80 : 0x00);

	const u32 partStart = params->partStart;
	const u32 heads     = params->heads;
	const u32 secPerTrk = params->secPerTrk;
	const u32 startChs  = lba2chs(partStart, heads, secPerTrk);
	memcpy(&mbr->partTable[0].startH, &startChs, 3);

	const u8 fatBits   = params->fatBits;
	const u64 totSec   = params->totSec;
	const u64 partSize = totSec - partStart; // TODO: Is this correct or should we align the end?
	u8 partType;
	if(fatBits == 12)               partType = 0x01; // FAT12 (<=16 MiB).
	else if(fatBits == 16)
	{
		if(partSize < 65536)        partType = 0x04; // FAT16 (<=32 MiB).
		else                        partType = 0x06; // FAT16 (>32 MiB).
	}
	else if(fatBits == 32)
	{
		if((totSec - 1) <= 16450560) partType = 0x0B; // FAT32 CHS.
		else                         partType = 0x0C; // FAT32 LBA.
	}
	else                             partType = 0x07; // exFAT.
	mbr->partTable[0].id = partType;
	verbosePrintf("Partition type: 0x%02" PRIX8 "\n", partType);

	const u32 endChs = lba2chs(totSec - 1, heads, secPerTrk); // TODO: totSec - 1 might be wrong.
	memcpy(&mbr->partTable[0].endH, &endChs, 3);

	mbr->partTable[0].startLba   = partStart;
	mbr->partTable[0].sectorsLba = (u32)partSize;
	mbr->magic                   = 0xAA55;
}

// TODO: Use template VBRs?
// TODO: Don't write it all at once. Write sectors or blocks as needed.
// Note: The memory pointed to by vbr should be cleared to zero before calling this function.
static void makeFsFat(const FormatParams *const params, Vbr *const vbr, const char *const label)
{
	char labelBuf[11];
	memset(labelBuf, ' ', sizeof(labelBuf)); // Padding must be spaces.
	const size_t labelLen = (label == NULL ? 7 : strlen(label)); // TODO: Maximum 11 chars.
	memcpy(labelBuf, (label == NULL ? "NO NAME" : label), labelLen);

	// Volume Boot Record (VBR).
	vbr->jmpBoot[0] = 0xEB;
	vbr->jmpBoot[1] = 0x00;
	vbr->jmpBoot[2] = 0x90;
	memcpy(vbr->oemName, "MSWIN4.1", 8); // SDFormatter seems to hardcode this?

	// BIOS Parameter Block (BPB).
	const u32 rsvdSecCnt = params->rsvdSecCnt;
	const u8  fatBits    = params->fatBits;
	const u32 partStart  = params->partStart;
	const u64 totSec     = params->totSec;
	const u64 partSize   = totSec - partStart; // TODO: Is this correct?
	const u32 secPerFat  = params->secPerFat;
	vbr->bytesPerSec = 512;
	vbr->secPerClus  = params->secPerClus;
	vbr->rsvdSecCnt  = rsvdSecCnt;
	vbr->numFats     = 2;
	vbr->rootEntCnt  = (fatBits == 32 ? 0 : 512);
	vbr->totSec16    = (partSize > 0xFFFF || fatBits == 32 ? 0 : partSize); // Not used for FAT32.
	vbr->media       = 0xF8;
	vbr->fatSz16     = (secPerFat > 0xFFFF || fatBits == 32 ? 0 : secPerFat); // Not used for FAT32.
	vbr->secPerTrk   = params->secPerTrk;
	vbr->numHeads    = params->heads;
	vbr->hiddSec     = partStart;
	vbr->totSec32    = (partSize > 0xFFFF || fatBits == 32 ? partSize : 0);
	vbr->sigWord     = 0xAA55;

	if(fatBits < 32)
	{
		// Extended BIOS Parameter Block FAT12/FAT16.
		vbr->fat16.drvNum  = 0x80;
		vbr->fat16.bootSig = 0x29;
		vbr->fat16.volId   = makeVolId();
		memcpy(vbr->fat16.volLab, labelBuf, 11);
		memcpy(vbr->fat16.filSysType, "FAT16   ", 8); // Bug: What about FAT12?
		memset(vbr->fat16.bootCode, 0xF4, sizeof(vbr->fat16.bootCode));

		// Reserve first 2 FAT entries of both FATs.
		u32 *const fat = (u32*)((u8*)vbr + (512 * rsvdSecCnt));
		const u32 fillVal = (fatBits == 16 ? 0xFFFFFFF8 : 0x00FFFFF8); // TODO: Check if the FAT12 value is correct.
		fat[0]            = fillVal;
		fat[secPerFat<<7] = fillVal;
	}
	else
	{
		// Extended BIOS Parameter Block FAT32.
		vbr->fat32.fatSz32      = secPerFat;
		vbr->fat32.extFlags     = 0; // TODO: Allow disabling mirroring?
		vbr->fat32.fsVer        = 0; // 0.0.
		vbr->fat32.rootClus     = 2; // 2 or the first cluster not marked as defective.
		vbr->fat32.fsInfoSector = 1;
		vbr->fat32.bkBootSec    = 6;
		vbr->fat32.drvNum       = 0x80;
		vbr->fat32.bootSig      = 0x29;
		vbr->fat32.volId        = makeVolId();
		memcpy(vbr->fat32.volLab, labelBuf, 11);
		memcpy(vbr->fat32.filSysType, "FAT32   ", 8);
		memset(vbr->fat32.bootCode, 0xF4, sizeof(vbr->fat32.bootCode));

		FsInfo *const fsInfo = (FsInfo*)(vbr + 1);
		fsInfo->leadSig   = 0x41615252;
		fsInfo->strucSig  = 0x61417272;
		fsInfo->freeCount = params->maxClus - 1;
		fsInfo->nxtFree   = 3;
		fsInfo->trailSig  = 0xAA550000;

		// The unused sector after FSInfo does also have a signature word.
		(vbr + 2)->sigWord = 0xAA55;

		// Copy boot sector, FSInfo sector and the third one to the backup location.
		memcpy(vbr + 6, vbr, 512 * 3);

		// Free cluster count is 0xFFFFFFFF (unknown) for FSInfo copy.
		(fsInfo + 6)->freeCount = 0xFFFFFFFF;

		// Reserve first 2 FAT entries of both FATs. A third entry for the root directory cluster.
		u32 *const fat = (u32*)((u8*)vbr + (512 * rsvdSecCnt));
		fat[0]                  = 0x0FFFFFF8;
		fat[1]                  = 0x0FFFFFFF;
		fat[2]                  = 0x0FFFFFFF; // Root directory cluster.
		fat[(secPerFat<<7) + 0] = 0x0FFFFFF8;
		fat[(secPerFat<<7) + 1] = 0x0FFFFFFF;
		fat[(secPerFat<<7) + 2] = 0x0FFFFFFF; // Root directory cluster.
	}

	// Create volume label entry in root directory if needed.
	if(label != NULL)
	{
		FatDir *const dir = (FatDir*)((u8*)vbr + (512 * (rsvdSecCnt + secPerFat * 2)));
		memcpy(dir->name, labelBuf, 11);
		dir->attr = 0x08; // ATTR_VOLUME_ID.
		// All other fields should already be zero.
	}
}

/*static void makeFsExFat(const FormatParams *const params, Vbr *const vbr, const char *const label)
{
	// Volume Boot Record (VBR).
	// TODO
}*/

u32 formatSd(const char *const path, const char *const label, const ArgFlags flags, const u64 overrTotSec)
{
	BlockDev dev;
	if(dev.open(path, true) != 0) return 2;

	// Allow overriding the capacity only if we create a new image file
	// or the new capacity is lower.
	// TODO: Minimum capacity we can format in FAT12.
	// TODO: Error if capacity is higher than device sectors.
	u64 totSec = dev.getSectors();
	if(overrTotSec > 0 && (totSec == 0 || overrTotSec < totSec))
	{
		// Only truncate regular files.
		if(totSec == 0) dev.truncate(overrTotSec);
		totSec = overrTotSec;
	}
	verbosePrintf("SD card contains %" PRIu64 " sectors.\n", totSec);

	if(flags.erase || flags.secErase)
	{
		verbosePuts("Erasing SD card...");

		// TODO: Apparently not all cards support secure erase.
		const int discardRes = dev.discardAll(flags.secErase);
		if(discardRes == EOPNOTSUPP)
		{
			fputs("SD card can't be erased. Ignoring.\n", stderr);
		}
		else if(discardRes != 0) return 3;
	}

	FormatParams params{};
	getFormatParams(totSec, flags.forceFat32, &params);

	// For FAT32 we also need to clear the root directory cluster.
	const u32 partStart = params.partStart;
	const size_t bufSize = 512 * (partStart + params.fsAreaSize + (params.fatBits == 32 ? params.secPerClus : 0));
	const std::unique_ptr<u8[]> buf(new(std::nothrow) u8[bufSize]{});
	if(!buf)
	{
		fputs("Not enough memory.", stderr);
		return 3;
	}

	// Create a new Master Boot Record and partition.
	// TODO: Allow to make it bootable?
	verbosePuts("Creating new partition table and partition...");
	makeMbrPartition(&params, false, (Mbr*)buf.get());

	// Clear filesystem areas and write a new Volume Boot Record.
	// TODO: Label should be upper case and some chars are not allowed. Implement conversion + checks.
	//       mkfs.fat allows lower case but warns about it.
	verbosePuts("Formatting the partition...");
	makeFsFat(&params, (Vbr*)(buf.get() + (512 * partStart)), label);

	if(dev.write(buf.get(), 0, bufSize / 512) != 0)
		return 4;

	puts("Successfully formatted the card.");
	printFormatParams(&params);

	return 0;
}

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "types.h"
#include "fat.h"
#include "verbose_printf.h"



// FAT12/FAT16.
void calcFormatFat(const u32 totSec, FormatParams &params)
{
	const u32 fatBits     = params.fatBits;
	const u32 alignment   = params.alignment;
	const u32 secPerClus  = params.secPerClus;
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

	params.rsvdSecCnt = rsvdSecCnt;
	params.secPerFat  = secPerFat;
	params.fsAreaSize = fsAreaSize;
	params.partStart  = partStart;
	params.maxClus    = maxClus;
}

// TODO: Using u32 for totSec limits us to <2 TiB maximum capacity.
void calcFormatFat32(const u32 totSec, FormatParams &params)
{
	const u32 fatBits     = 32;
	const u32 bytesPerSec = 512;
	const u32 alignment   = params.alignment;
	u32 secPerClus        = params.secPerClus;

	if(secPerClus > 128)
	{
		fputs("Warning: FAT32 doesn't support more than 64 KiB per cluster."
		      " Using 64 which might lower performance and lifetime.\n", stderr);
		params.secPerClus = 128;
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

	params.rsvdSecCnt = rsvdSecCnt;
	params.secPerFat  = secPerFat;
	params.fsAreaSize = fsAreaSize;
	params.partStart  = partStart;
	params.maxClus    = maxClus;
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

struct FsSectors
{
	Vbr vbr;
	FsInfo fsInfo;
};
static_assert(sizeof(FsSectors) == 1024, "FsSectors must have a size of 1024 bytes.");

int makeFsFat(const FormatParams &params, BufferedFsWriter &dev, const std::string &label)
{
	// Seek ahead to partition start and fill everything inbetween with zeros.
	const u32 partStart = params.partStart;
	u64 curOffset = partStart * 512;
	int res = dev.fill(curOffset);
	if(res != 0) return res;

	// Prepare label.
	char labelBuf[12] = "NO NAME    ";
	if(!label.empty())
	{
		memset(labelBuf, ' ', 11); // Padding must be spaces.
		size_t labelLen = label.size();
		labelLen = (labelLen > 11 ? 11 : labelLen);
		memcpy(labelBuf, label.c_str(), labelLen);
	}

	FsSectors fsSectors{};

	// Volume Boot Record (VBR).
	Vbr &vbr = fsSectors.vbr;
	vbr.jmpBoot[0] = 0xEB;
	vbr.jmpBoot[1] = 0x00;
	vbr.jmpBoot[2] = 0x90;
	memcpy(vbr.oemName, "MSWIN4.1", 8); // SDFormatter hardcodes this.

	// BIOS Parameter Block (BPB).
	const u32 secPerClus = params.secPerClus;
	const u32 rsvdSecCnt = params.rsvdSecCnt;
	const u8  fatBits    = params.fatBits;
	const u32 partSize   = static_cast<u32>(params.totSec - partStart);
	const u32 secPerFat  = params.secPerFat;
	vbr.bytesPerSec = 512;
	vbr.secPerClus  = secPerClus;
	vbr.rsvdSecCnt  = rsvdSecCnt;
	vbr.numFats     = 2;
	vbr.rootEntCnt  = (fatBits == 32 ? 0 : 512);
	vbr.totSec16    = (partSize > 0xFFFF || fatBits == 32 ? 0 : partSize);   // Not used for FAT32.
	vbr.media       = 0xF8;
	vbr.fatSz16     = (secPerFat > 0xFFFF || fatBits == 32 ? 0 : secPerFat); // Not used for FAT32.
	vbr.secPerTrk   = params.secPerTrk;
	vbr.numHeads    = params.heads;
	vbr.hiddSec     = partStart;
	vbr.totSec32    = (partSize > 0xFFFF || fatBits == 32 ? partSize : 0);
	vbr.sigWord     = 0xAA55;

	if(fatBits < 32)
	{
		// Extended BIOS Parameter Block FAT12/FAT16.
		vbr.fat16.drvNum  = 0x80;
		vbr.fat16.bootSig = 0x29;
		vbr.fat16.volId   = makeVolId();
		memcpy(vbr.fat16.volLab, labelBuf, 11);
		memcpy(vbr.fat16.filSysType, (fatBits == 12 ? "FAT12   " : "FAT16   "), 8);
		memset(vbr.fat16.bootCode, 0xF4, sizeof(vbr.fat16.bootCode));

		// Write Vbr.
		res = dev.write(reinterpret_cast<u8*>(&vbr), sizeof(Vbr));
		if(res != 0) return res;
	}
	else
	{
		// Extended BIOS Parameter Block FAT32.
		vbr.fat32.fatSz32      = secPerFat;
		vbr.fat32.extFlags     = 0;
		vbr.fat32.fsVer        = 0; // 0.0.
		vbr.fat32.rootClus     = 2; // 2 or the first cluster not marked as defective.
		vbr.fat32.fsInfoSector = 1;
		vbr.fat32.bkBootSec    = 6;
		vbr.fat32.drvNum       = 0x80;
		vbr.fat32.bootSig      = 0x29;
		vbr.fat32.volId        = makeVolId();
		memcpy(vbr.fat32.volLab, labelBuf, 11);
		memcpy(vbr.fat32.filSysType, "FAT32   ", 8);
		memset(vbr.fat32.bootCode, 0xF4, sizeof(vbr.fat32.bootCode));

		FsInfo &fsInfo = fsSectors.fsInfo;
		fsInfo.leadSig   = 0x41615252;
		fsInfo.strucSig  = 0x61417272;
		fsInfo.freeCount = params.maxClus - 1;
		fsInfo.nxtFree   = 3;
		fsInfo.trailSig  = 0xAA550000;

		// Write Vbr and FSInfo.
		res = dev.write(reinterpret_cast<u8*>(&fsSectors), sizeof(FsSectors));
		if(res != 0) return res;

		// Write unused sector with signature word.
		res = dev.fillAndWrite(reinterpret_cast<u8*>(&vbr.sigWord), curOffset + sizeof(FsSectors) + 510, 2);
		if(res != 0) return res;

		// Free cluster count is 0xFFFFFFFF (unknown) for FSInfo copy.
		// Write copy of Vbr and FSInfo.
		fsInfo.freeCount = 0xFFFFFFFF;
		res = dev.fillAndWrite(reinterpret_cast<u8*>(&fsSectors), curOffset + (6 * 512), sizeof(FsSectors));
		if(res != 0) return res;

		// Write copy of unused sector with signature word.
		res = dev.fillAndWrite(reinterpret_cast<u8*>(&vbr.sigWord), curOffset + (8 * 512) + 510, 2);
		if(res != 0) return res;
	}

	// Prepare reserved FAT entries.
	u32 rsvdEntrySize = 4;
	if(fatBits < 32)
	{
		// Reserve first 2 FAT entries.
		u32 *const fat = reinterpret_cast<u32*>(&fsSectors);
		*fat = (fatBits == 16 ? 0xFFFFFFF8 : 0x00FFFFF8);
	}
	else
	{
		// Reserve first 2 FAT entries. A third entry for the root directory cluster.
		u32 *const fat = reinterpret_cast<u32*>(&fsSectors);
		fat[0] = 0x0FFFFFF8;
		fat[1] = 0x0FFFFFFF;
		fat[2] = 0x0FFFFFFF;
		rsvdEntrySize = 3 * 4;
	}

	// Write first FAT.
	curOffset += rsvdSecCnt * 512;
	res = dev.fillAndWrite(reinterpret_cast<u8*>(&fsSectors), curOffset, rsvdEntrySize);
	if(res != 0) return res;

	// Write second FAT.
	curOffset += secPerFat * 512;
	res = dev.fillAndWrite(reinterpret_cast<u8*>(&fsSectors), curOffset, rsvdEntrySize);
	if(res != 0) return res;

	// Create volume label entry in root directory if needed.
	if(!label.empty())
	{
		FatDir *const dir = reinterpret_cast<FatDir*>(&fsSectors);
		memset(dir, 0, sizeof(FatDir));  // Make sure all other fields are zero.
		memcpy(dir->name, labelBuf, 11);
		dir->attr = 0x08;                // ATTR_VOLUME_ID.

		curOffset += secPerFat * 512;
		res = dev.fillAndWrite(reinterpret_cast<u8*>(dir), curOffset, sizeof(FatDir));
		if(res != 0) return res;
	}

	// Fill rest of FS area and root directory cluster for FAT32.
	curOffset = (partStart + params.fsAreaSize + (fatBits < 32 ? 0 : secPerClus)) * 512;
	return dev.fill(curOffset);
}

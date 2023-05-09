// SPDX-License-Identifier: MIT
// Copyright (c) 2023 profi200

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "types.h"
#include "fat.h"
#include "util.h"
#include "verbose_printf.h"



// FAT12/FAT16.
void calcFormatFat(FormatParams &params)
{
	const u32 totSec          = params.totSec & 0xFFFFFFFFu;
	const u32 fatBits         = params.fatBits;
	const u32 alignment       = params.alignment;
	const u32 secPerClus      = params.secPerClus;
	constexpr u32 bytesPerSec = 512; // Can be hardcoded (no big logical sector support for FAT12/16).
	constexpr u32 rootEntCnt  = 512;
	constexpr u32 rsvdSecCnt  = 1;
	u32 secPerFat             = util::udivCeil(totSec / secPerClus * fatBits, bytesPerSec * 8);
	u32 fsAreaSize;
	u32 partStart;
	u32 maxClus;
	while(1)
	{
		fsAreaSize = rsvdSecCnt + 2 * secPerFat + util::udivCeil(32 * rootEntCnt, bytesPerSec);
		partStart  = alignment - fsAreaSize % alignment;
		if(partStart != alignment) partStart += alignment;

		u32 tmpSecPerFat;
		while(1)
		{
			maxClus      = (totSec - partStart - fsAreaSize) / secPerClus;
			tmpSecPerFat = util::udivCeil((2 + maxClus) * fatBits, bytesPerSec * 8);

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

void calcFormatFat32(FormatParams &params)
{
	const u32 totSec      = params.totSec & 0xFFFFFFFFu;
	constexpr u32 fatBits = 32;
	const u32 bytesPerSec = params.bytesPerSec;
	const u32 alignment   = params.alignment;
	const u32 secPerClus  = params.secPerClus;
	u32 secPerFat         = util::udivCeil(totSec / secPerClus * fatBits, bytesPerSec * 8);
	const u32 partStart   = alignment;
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
			tmpSecPerFat = util::udivCeil((2 + maxClus) * fatBits, bytesPerSec * 8);

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
u32 makeVolId(void)
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

int makeFsFat(const FormatParams &params, BufferedFsWriter &dev, const std::string &label)
{
	// Seek ahead to partition start and fill everything inbetween with zeros.
	const u32 partStart = params.partStart;
	const u32 bytesPerSec = params.bytesPerSec;
	u64 curOffset = partStart * bytesPerSec;
	int res = dev.fill(curOffset);
	if(res != 0) return res;

	// Prepare label.
	char labelBuf[12] = EBPB_VOL_LAB_NO_NAME;
	if(!label.empty())
	{
		memset(labelBuf, ' ', 11); // Padding must be spaces.
		size_t labelLen = label.size();
		labelLen = (labelLen > 11 ? 11 : labelLen);
		memcpy(labelBuf, label.c_str(), labelLen);
	}

	// Boot sector.
	BootSec bs{};
	const u8 fatBits = params.fatBits;
	const u8 jmpBoot[3] = {0xEB, (u8)(fatBits < 32 ? 0x3C : 0x58), 0x90}; // Note: SDFormatter hardcodes 0xEB 0x00 0x90.
	memcpy(bs.jmpBoot, jmpBoot, 3);
	memcpy(bs.oemName, BS_DEFAULT_OEM_NAME, 8);

	// BIOS Parameter Block (BPB).
	const u32 secPerClus  = params.secPerClus;
	const u32 rsvdSecCnt  = params.rsvdSecCnt;
	const u32 partSectors = static_cast<u32>(params.totSec - partStart);
	const u32 secPerFat   = params.secPerFat;
	bs.bytesPerSec = bytesPerSec;
	bs.secPerClus  = secPerClus;
	bs.rsvdSecCnt  = rsvdSecCnt;
	bs.numFats     = 2;
	bs.rootEntCnt  = (fatBits == 32 ? 0 : 512);
	bs.totSec16    = (partSectors > 0xFFFF || fatBits == 32 ? 0 : partSectors); // Not used for FAT32.
	bs.media       = BPB_DEFAULT_MEDIA;
	bs.fatSz16     = (secPerFat > 0xFFFF || fatBits == 32 ? 0 : secPerFat);     // Not used for FAT32.
	bs.secPerTrk   = params.secPerTrk;
	bs.numHeads    = params.heads;
	bs.hiddSec     = partStart;
	bs.totSec32    = (partSectors > 0xFFFF || fatBits == 32 ? partSectors : 0);
	bs.sigWord     = EBPB_SIG_WORD;

	if(fatBits < 32)
	{
		// Extended BIOS Parameter Block FAT12/FAT16.
		bs.ebpb.drvNum  = EBPB_DEFAULT_DRV_NUM;
		bs.ebpb.bootSig = EBPB_BOOT_SIG;
		bs.ebpb.volId   = makeVolId();
		memcpy(bs.ebpb.volLab, labelBuf, 11);
		memcpy(bs.ebpb.filSysType, (fatBits == 12 ? EBPB_FIL_SYS_TYPE_FAT12 : EBPB_FIL_SYS_TYPE_FAT16), 8);
		memset(bs.ebpb.bootCode, 0xF4, sizeof(bs.ebpb.bootCode)); // Fill with x86 hlt instructions.

		// Write boot sector.
		res = dev.write(&bs, sizeof(BootSec));
		if(res != 0) return res;
	}
	else
	{
		// Extended BIOS Parameter Block FAT32.
		bs.ebpb32.fatSz32      = secPerFat;
		bs.ebpb32.extFlags     = 0;
		bs.ebpb32.fsVer        = 0; // 0.0.
		bs.ebpb32.rootClus     = 2; // 2 or the first cluster not marked as defective.
		bs.ebpb32.fsInfoSector = 1;
		bs.ebpb32.bkBootSec    = 6;
		bs.ebpb32.drvNum       = EBPB_DEFAULT_DRV_NUM;
		bs.ebpb32.bootSig      = EBPB_BOOT_SIG;
		bs.ebpb32.volId        = makeVolId();
		memcpy(bs.ebpb32.volLab, labelBuf, 11);
		memcpy(bs.ebpb32.filSysType, EBPB_FIL_SYS_TYPE_FAT32, 8);
		memset(bs.ebpb32.bootCode, 0xF4, sizeof(bs.ebpb32.bootCode)); // Fill with x86 hlt instructions.

		// Write boot sector.
		res = dev.write(&bs, sizeof(BootSec));
		if(res != 0) return res;

		// There are apparently drivers based on wrong documentation stating the
		// signature word is at end of sector instead of fixed offset 510.
		// Fill up to sector size and write the signature word to make them work.
		u64 tmpOffset;
		if(bytesPerSec > 512)
		{
			tmpOffset = curOffset + bytesPerSec - 2;
			res = dev.fillAndWrite(&bs.sigWord, tmpOffset, 2);
			if(res != 0) return res;
		}

		// Write FSInfo.
		FsInfo fsInfo{};
		fsInfo.leadSig   = FS_INFO_LEAD_SIG;
		fsInfo.strucSig  = FS_INFO_STRUC_SIG;
		fsInfo.freeCount = params.maxClus - 1; // One cluster is reserved for root directory.
		fsInfo.nxtFree   = 3;
		fsInfo.trailSig  = FS_INFO_TRAIL_SIG;
		res = dev.write(&fsInfo, sizeof(FsInfo));
		if(res != 0) return res;

		// The FAT spec says there is actually a third boot sector with just a signature word.
		tmpOffset = curOffset + (2 * bytesPerSec) + bytesPerSec - 2;
		res = dev.fillAndWrite(&bs.sigWord, tmpOffset, 2);
		if(res != 0) return res;

		// Write copy of boot sector.
		tmpOffset += 2 + (3 * bytesPerSec);
		res = dev.fillAndWrite(&bs, tmpOffset, sizeof(BootSec));
		if(res != 0) return res;

		// Write sector signature word of boot sector copy.
		if(bytesPerSec > 512)
		{
			tmpOffset += bytesPerSec - 2;
			res = dev.fillAndWrite(&bs.sigWord, tmpOffset, 2);
			if(res != 0) return res;
		}

		// Free cluster count is unknown for FSInfo copy.
		fsInfo.freeCount = FS_INFO_UNK_FREE_COUNT;
		res = dev.write(&fsInfo, sizeof(FsInfo));
		if(res != 0) return res;

		// Write copy of third sector signature word.
		tmpOffset = curOffset + (8 * bytesPerSec) + bytesPerSec - 2;
		res = dev.fillAndWrite(&bs.sigWord, tmpOffset, 2);
		if(res != 0) return res;
	}

	// Prepare reserved FAT entries.
	u32 rsvdEntrySize = 4;
	u32 fat[3];
	if(fatBits < 32)
	{
		// Reserve first 2 FAT entries.
		u32 rsvdEnt = (fatBits == 16 ? FAT16_EOF<<16 | FAT16_EOF : FAT12_EOF<<12 | FAT12_EOF);
		*fat = (rsvdEnt & ~0xFFu) | BPB_DEFAULT_MEDIA;
	}
	else
	{
		// Reserve first 2 FAT entries. A third entry for the root directory cluster.
		fat[0] = (FAT32_EOF & ~0xFFu) | BPB_DEFAULT_MEDIA;
		fat[1] = FAT32_EOF;
		fat[2] = FAT32_EOF;
		rsvdEntrySize = 3 * 4;
	}

	// Write first FAT.
	curOffset += rsvdSecCnt * bytesPerSec;
	res = dev.fillAndWrite(fat, curOffset, rsvdEntrySize);
	if(res != 0) return res;

	// Write second FAT.
	curOffset += secPerFat * bytesPerSec;
	res = dev.fillAndWrite(fat, curOffset, rsvdEntrySize);
	if(res != 0) return res;

	// Create volume label entry in root directory if needed.
	if(!label.empty())
	{
		FatDirEnt dir{};                // Make sure all other fields are zero.
		memcpy(dir.name, labelBuf, 11);
		dir.attr = DIR_ATTR_VOLUME_ID;

		curOffset += secPerFat * bytesPerSec;
		res = dev.fillAndWrite(&dir, curOffset, sizeof(FatDirEnt));
		if(res != 0) return res;
	}

	// Fill rest of FS area and root directory cluster for FAT32.
	curOffset = (partStart + params.fsAreaSize + (fatBits < 32 ? 0 : secPerClus)) * bytesPerSec;
	return dev.fill(curOffset);
}

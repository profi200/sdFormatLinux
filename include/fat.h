#pragma once

#include <cstddef>
#include "types.h"
#include "format.h"
#include "buffered_fs_writer.h"

// References:
// FAT12/16/32: http://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/fatgen103.doc


// Volume Boot Record.
typedef struct
{
	u8 jmpBoot[3];   // {0xEB, 0xXX, 0x90} or {0xE9, 0xXX, 0xXX}.
	char oemName[8]; // Usually system name that formatted the volume.

	// BIOS Parameter Block.
	u16 bytesPerSec; // 512, 1024, 2048 or 4096. Usually 512.
	u8 secPerClus;   // 1, 2, 4, 8, 16, 32, 64 or 128.
	u16 rsvdSecCnt;  // Must not be 0.
	u8 numFats;      // Should be 2. 1 is also allowed.
	u16 rootEntCnt;  // 0 for FAT32.
	u16 totSec16;    // Must be zero for FAT32.
	u8 media;        // 0xF0, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE or 0xFF. Usually 0xF8.
	u16 fatSz16;     // Must be zero for FAT32.
	u16 secPerTrk;
	u16 numHeads;
	u32 hiddSec;     // Number of sectors preceding the partition.
	u32 totSec32;    // Must be non-zero for FAT32.

	// Extended BIOS Parameter Block.
	union
	{
		struct __attribute__((packed)) // FAT12/FAT16.
		{
			u8 drvNum;          // 0x80 or 0x00.
			u8 reserved1;       // Must be 0.
			u8 bootSig;         // 0x29 if one or both of the following 2 fields are non-zero.
			u32 volId;          // Volume serial number generated from date and time.
			char volLab[11];    // "NO NAME    " or space padded label.
			char filSysType[8]; // "FAT12   ", "FAT16   " or "FAT     ".
			u8 bootCode[448];
		} fat16;
		struct __attribute__((packed)) // FAT32.
		{
			u32 fatSz32;        // Must be non-zero.
			u16 extFlags;       // Usually 0. Bits 0-3: Active FAT. Bit 7: 1 = disable mirroring (see bits 0-3).
			u16 fsVer;          // Must be 0.
			u32 rootClus;       // Should be 2 or the first non-defective cluster.
			u16 fsInfoSector;   // Usually 1.
			u16 bkBootSec;      // 0 or 6. Backup boot sector must be present if the later.
			u8 reserved[12];    // Must be 0.
			u8 drvNum;          // 0x80 or 0x00.
			u8 reserved1;       // Must be 0.
			u8 bootSig;         // 0x29 if one or both of the following 2 fields are non-zero.
			u32 volId;          // Volume serial number generated from date and time.
			char volLab[11];    // "NO NAME    " or space padded label.
			char filSysType[8]; // "FAT32   ".
			u8 bootCode[420];
		} fat32;
	};

	u16 sigWord; // 0xAA55.
} __attribute__((packed)) Vbr;
static_assert(offsetof(Vbr, fat16.bootCode) == 62, "Member fat16.bootCode of Vbr not at offsetof 62.");
static_assert(offsetof(Vbr, fat32.bootCode) == 90, "Member fat32.bootCode of Vbr not at offsetof 90.");
static_assert(offsetof(Vbr, sigWord) == 510, "Member sigWord of Vbr not at offsetof 510.");

typedef struct
{
	u32 leadSig;       // Must be 0x41615252.
	u8 reserved1[480]; // Must be 0.
	u32 strucSig;      // Must be 0x61417272.
	u32 freeCount;     // Number of free clusters or 0xFFFFFFFF if unknown.
	u32 nxtFree;       // First free cluster number or 0xFFFFFFFF if unknown.
	u8 reserved2[12];  // Must be 0.
	u32 trailSig;      // Must be 0xAA550000.
} FsInfo;
static_assert(offsetof(FsInfo, trailSig) == 508, "Member trailSig of FsInfo not at offsetof 508.");

typedef struct
{
	char name[11];   // Short file name in 8:3 format. Maximum 11 characters.
	u8 attr;         // Attribute bitmask. ATTR_READ_ONLY 0x01, ATTR_HIDDEN 0x02, ATTR_SYSTEM 0x04, ATTR_VOLUME_ID 0x08, ATTR_DIRECTORY 0x10, ATTR_ARCHIVE 0x20.
	u8 ntRes;        // Must be 0.
	u8 crtTimeTenth; // Creation time tenths of a second.
	u16 crtTime;     // Creation time in 2 second units.
	u16 crtDate;     // Creation date.
	u16 lstAccDate;  // Last access date. Updated on write.
	u16 fstClusHi;   // High u16 of first data cluster. Must be 0 for FAT12/16.
	u16 wrtTime;     // Last (modification) write time.
	u16 wrtDate;     // Last (modification) write date.
	u16 fstClusLo;   // Low u16 of first data cluster.
	u32 fileSize;    // File/directory size in bytes.
} FatDir;
static_assert(offsetof(FatDir, fileSize) == 28, "Member fileSize of FatDir not at offsetof 28.");



void calcFormatFat(const u32 totSec, FormatParams &params);
void calcFormatFat32(const u32 totSec, FormatParams &params);
int makeFsFat(const FormatParams &params, BufferedFsWriter &dev, const std::string &label);

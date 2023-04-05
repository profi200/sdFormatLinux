#pragma once

// SPDX-License-Identifier: MIT

#include <cstddef>
#include "types.h"
#include "format.h"
#include "buffered_fs_writer.h"

// References:
// https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification


// Boot Sector.
typedef struct
{
	u8  jumpBoot[3];                  // {0xEB, 0x76, 0x90}.
	char fileSystemName[8];          // "EXFAT   ".
	u8  mustBeZero[53];
	u64 partitionOffset;             // Arbitrary value or 0 = ignore this field.
	u64 volumeLength;                // Minimum "2^20 / 2^BytesPerSectorShift", maximum "2^64 - 1". If excess space sub-region size = 0 then max. is "ClusterHeapOffset + (2^32 - 11) * 2^SectorsPerClusterShift".
	u32 fatOffset;                   // Minimum "24", maximum "ClusterHeapOffset - (FatLength * NumberOfFats)".
	u32 fatLength;                   // Minimum "(ClusterCount + 2) * 2^2 / 2^BytesPerSectorShift" rounded up to nearest integer, maximum "(ClusterHeapOffset - FatOffset) / NumberOfFats" rounded down to nearest integer.
	u32 clusterHeapOffset;           // Minimum "FatOffset + FatLength * NumberOfFats", maximum "2^32 - 1" or "VolumeLength - (ClusterCount * 2^SectorsPerClusterShift)" whichever is smaller.
	u32 clusterCount;                // Shall be the lesser of "(VolumeLength - ClusterHeapOffset) / 2^SectorsPerClusterShift rounded down to the nearest integer" or "2^32 - 11". Recommended no more than "2^24 - 2".
	u32 firstClusterOfRootDirectory; // Minimum "2", maximum "ClusterCount + 1".
	u32 volumeSerialNumber;          // Volume serial number generated from date and time.
	u16 fileSystemRevision;          // versionHigh<<8 | (u8)versionLow. Version high 1-99, versionLow 0-99. Usually 1.00.
	u16 volumeFlags;                 // Bit 0 = activeFat, bit 1 = volumeDirty, bit 2 = mediaFailure, bit 3 = clearToZero, bits 4-15 reserved.
	u8  bytesPerSectorShift;         // Minimum "9" (512 bytes), maximum "12" (4 KiB).
	u8  sectorsPerClusterShift;      // Minimum "0" (1 sector), maximum "25 - BytesPerSectorShift" (32 MiB).
	u8  numberOfFats;                // "1" or "2" (TexFAT only).
	u8  driveSelect;                 // Arbitrary value. Recommended 0x80.
	u8  percentInUse;                // 0-100 "percentage of allocated clusters in the Cluster Heap, rounded down to the nearest integer" or 0xFF if unknown.
	u8  reserved[7];
	u8  bootCode[390];               // Bootstrapping code or 0xF4 filled (x86 halt).
	u16 bootSignature;               // 0xAA55.
	//u8 excessSpace[(1u<<bytesPerSectorShift) - 512];
} ExfatBootSec;
static_assert(offsetof(ExfatBootSec, bootSignature) == 510, "Member bootSignature of ExfatBootSec not at offsetof 510.");

typedef struct
{
	u8  guid[16];         // {0A0C7E46-3399-4021-90C8-FA6D389C4BA2} (467E0C0A 9933 2140 90C8 FA6D389C4BA2 in memory).
	u32 eraseBlockSize;
	u32 pageSize;
	u32 spareSectors;
	u32 randomAccessTime;
	u32 programmingTime;
	u32 readCycle;
	u32 writeCycle;
	u32 reserved;
} FlashParameters;
static_assert(offsetof(FlashParameters, reserved) == 44, "Member reserved of FlashParameters not at offsetof 44.");

typedef struct
{
	u8 entryType;
	union
	{
		struct __attribute__((packed))
		{
			u8  bitmapFlags;
			u8  reserved[18];
			u32 firstCluster;
			u64 dataLength;
		} bitmap;
		struct __attribute__((packed))
		{
			u8  reserved1[3];
			u32 tableChecksum;
			u8  reserved2[12];
			u32 firstCluster;
			u64 dataLength;
		} upCase;
		struct __attribute__((packed))
		{
			u8  characterCount;
			u16 volumeLabel[11];
			u8  reserved[8];
		} label;
		// TODO: Other entry types.
	};
} ExfatDirEnt;
static_assert(sizeof(ExfatDirEnt) == 32, "ExfatDirEnt is not 32 bytes.");


// Boot Sector.
#define BS_JUMP_BOOT           "\xEB\x76\x90"
#define BS_FILE_SYS_NAME       "EXFAT   "
#define BS_FILE_SYS_REV_1_00   (1u<<8 | 0) // 1.00.
#define BS_DRIVE_SELECT        (0x80u)
#define BS_BOOT_SIG            (0xAA55u)

// Extended Boot Sectors.
#define EBS_EXT_BOOT_SIG       (0xAA550000u)

// OEM Parameters.
#define OEM_FLASH_PARAMS_GUID  "\x46\x7E\x0C\x0A\x99\x33\x21\x40\x90\xC8\xFA\x6D\x38\x9C\x4B\xA2"

// File Allocation Table.
#define EXFAT_FIRST_ENT        (2u)          // Index 0 and 1 are reserved.
#define EXFAT_MAX_CLUS         (0xFFFFFFF5u) // "2^32 - 11, which is the maximum number of clusters a FAT can describe"

#define EXFAT_FREE             (0u)
#define EXFAT_BAD              (0xFFFFFFF7u)
#define EXFAT_RESERVED         (0xFFFFFFF8u)
#define EXFAT_EOF              (0xFFFFFFFFu)

// Directory Entry.
// Bits 0-4 TypeCode.
#define TYPE_IMPORTANCE_CRITICAL  (0u)
#define TYPE_IMPORTANCE_BENIGN    (1u<<5)
#define TYPE_CATEGORY_PRIMARY     (0u)
#define TYPE_CATEGORY_SECONDARY   (1u<<6)
#define TYPE_IN_USE               (1u<<7)

#define TYPE_END_OF_DIR           (                TYPE_CATEGORY_PRIMARY | TYPE_IMPORTANCE_CRITICAL | 0u)
#define TYPE_INVALID              (TYPE_IN_USE |   TYPE_CATEGORY_PRIMARY | TYPE_IMPORTANCE_CRITICAL | 0u)
#define TYPE_BITMAP               (TYPE_IN_USE |   TYPE_CATEGORY_PRIMARY | TYPE_IMPORTANCE_CRITICAL | 1u)
#define TYPE_UP_CASE              (TYPE_IN_USE |   TYPE_CATEGORY_PRIMARY | TYPE_IMPORTANCE_CRITICAL | 2u)
#define TYPE_VOL_LABEL            (TYPE_IN_USE |   TYPE_CATEGORY_PRIMARY | TYPE_IMPORTANCE_CRITICAL | 3u)
#define TYPE_FILE                 (TYPE_IN_USE |   TYPE_CATEGORY_PRIMARY | TYPE_IMPORTANCE_CRITICAL | 5u) // Or directory.
#define TYPE_GUID                 (TYPE_IN_USE |   TYPE_CATEGORY_PRIMARY |   TYPE_IMPORTANCE_BENIGN | 0u)
#define TYPE_TEXFAT_PADDING       (TYPE_IN_USE |   TYPE_CATEGORY_PRIMARY |   TYPE_IMPORTANCE_BENIGN | 1u)
#define TYPE_WIN_CE_ACT           (TYPE_IN_USE |   TYPE_CATEGORY_PRIMARY |   TYPE_IMPORTANCE_BENIGN | 2u)
#define TYPE_STREAM               (TYPE_IN_USE | TYPE_CATEGORY_SECONDARY | TYPE_IMPORTANCE_CRITICAL | 0u)
#define TYPE_NAME                 (TYPE_IN_USE | TYPE_CATEGORY_SECONDARY | TYPE_IMPORTANCE_CRITICAL | 1u)
#define TYPE_WIN_CE_AC            (TYPE_IN_USE | TYPE_CATEGORY_SECONDARY | TYPE_IMPORTANCE_CRITICAL | 2u)



void calcFormatExFat(FormatParams &params);
int makeFsExFat(const FormatParams &params, BufferedFsWriter &dev, const std::u16string &label);

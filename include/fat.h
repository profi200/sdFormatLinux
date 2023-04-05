#pragma once

// SPDX-License-Identifier: MIT

#include <cstddef>
#include "types.h"
#include "format.h"
#include "buffered_fs_writer.h"

// References:
// http://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/fatgen103.doc


// Boot sector.
typedef struct __attribute__((packed))
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
			u8 reserved1;       // Must be 0. Used by Windows for dirty flag (bit 0 set = dirty).
			u8 bootSig;         // 0x29 if one or both of the following 2 fields are non-zero.
			u32 volId;          // Volume serial number generated from date and time.
			char volLab[11];    // "NO NAME    " or space padded label.
			char filSysType[8]; // "FAT12   ", "FAT16   " or "FAT     ".
			u8 bootCode[448];
		} ebpb;
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
			u8 reserved1;       // Must be 0. Used by Windows for dirty flag (bit 0 set = dirty).
			u8 bootSig;         // 0x29 if one or both of the following 2 fields are non-zero.
			u32 volId;          // Volume serial number generated from date and time.
			char volLab[11];    // "NO NAME    " or space padded label.
			char filSysType[8]; // "FAT32   ".
			u8 bootCode[420];
		} ebpb32;
	};
	u16 sigWord; // 0xAA55.
} BootSec;
static_assert(offsetof(BootSec, ebpb.bootCode) == 62, "Member ebpb.bootCode of BootSec not at offset 62.");
static_assert(offsetof(BootSec, ebpb32.bootCode) == 90, "Member ebpb32.bootCode of BootSec not at offset 90.");
static_assert(offsetof(BootSec, sigWord) == 510, "Member sigWord of BootSec not at offset 510.");

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
static_assert(offsetof(FsInfo, trailSig) == 508, "Member trailSig of FsInfo not at offset 508.");

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
} FatDirEnt;
static_assert(offsetof(FatDirEnt, fileSize) == 28, "Member fileSize of FatDirEnt not at offset 28.");

typedef struct __attribute__((packed))
{
	u8 ord;        // Order of LDIR entries. Last entry (which comes first) must have LAST_LONG_ENTRY (0x40) set.
	u16 name1[5];  // UTF-16 character 1-5 of long name.
	u8 attr;       // Must be ATTR_LONG_NAME (DIR_ATTR_VOLUME_ID | DIR_ATTR_SYSTEM | DIR_ATTR_HIDDEN | DIR_ATTR_READ_ONLY).
	u8 type;       // Must be 0 (sub-component of long name). Other values unknown (extensions).
	u8 chksum;     // Checksum of 11 characters short name.
	u16 name2[6];  // UTF-16 character 6-11 of long name.
	u16 fstClusLo; // Must be 0.
	u16 name3[2];  // UTF-16 character 12-13 of long name.
} FatLdirEnt;
static_assert(offsetof(FatLdirEnt, name3) == 28, "Member name3 of FatLdirEnt not at offset 28.");


// Boot sector.
#define BS_JMP_BOOT_FAT           "\xEB\x3C\x90"
#define BS_JMP_BOOT_FAT32         "\xEB\x58\x90"
#define BS_DEFAULT_OEM_NAME       "MSWIN4.1"     // Recommended default OEM name.

// BIOS Parameter Block.
#define BPB_DEFAULT_MEDIA         (0xF8u)

// Extended BIOS Parameter Block.
#define EBPB_DEFAULT_DRV_NUM      (0x80u)
#define EBPB_BOOT_SIG             (0x29u)
#define EBPB_VOL_LAB_NO_NAME      "NO NAME    "
#define EBPB_FIL_SYS_TYPE_FAT12   "FAT12   "
#define EBPB_FIL_SYS_TYPE_FAT16   "FAT16   "
#define EBPB_FIL_SYS_TYPE_FAT32   "FAT32   "
#define EBPB_SIG_WORD             (0xAA55u)

// FSInfo.
#define FS_INFO_LEAD_SIG          (0x41615252u)
#define FS_INFO_STRUC_SIG         (0x61417272u)
#define FS_INFO_UNK_FREE_COUNT    (0xFFFFFFFFu)
#define FS_INFO_UNK_NXT_FREE      (0xFFFFFFFFu)
#define FS_INFO_TRAIL_SIG         (0xAA550000u)

// FAT directory entry.
#define DIR_ATTR_READ_ONLY        (1u)
#define DIR_ATTR_HIDDEN           (1u<<1)
#define DIR_ATTR_SYSTEM           (1u<<2)
#define DIR_ATTR_VOLUME_ID        (1u<<3)
#define DIR_ATTR_DIRECTORY        (1u<<4)
#define DIR_ATTR_ARCHIVE          (1u<<5)

// File allocation table.
// Note: MAX_CLUS actually means number of clusters, not index!
#define FAT_FIRST_ENT             (2u)          // Index 0 and 1 are reserved.
#define FAT12_MAX_CLUS            (0xFF4u)      // Specification limit.
#define FAT16_MAX_CLUS            (0xFFF4u)     // Specification limit.
#define FAT32_MAX_CLUS            (0x0FFFFFF6u) // Theoretical limit. 2 clusters will not be allocatable. Spec limit is 0x0FFFFFF5?

#define FAT_FREE                  (0u)          // FAT entry is unallocated/free. Common for all 3 variants.
// 0xXXXXXFF6 is reserved.
#define FAT12_BAD                 (0xFF7u)
#define FAT16_BAD                 (0xFFF7u)
#define FAT32_BAD                 (0x0FFFFFF7u)
// 0xXXXXXFF8 to 0xXXXXXFFE is reserved.
#define FAT12_EOF                 (0xFFFu)
#define FAT16_EOF                 (0xFFFFu)
#define FAT32_EOF                 (0x0FFFFFFFu)

// FAT long directory entry.
#define LDIR_LAST_LONG_ENTRY      (1u<<6)
#define LDIR_ATTR_LONG_NAME       (DIR_ATTR_VOLUME_ID | DIR_ATTR_SYSTEM | DIR_ATTR_HIDDEN | DIR_ATTR_READ_ONLY)
#define LDIR_ATTR_LONG_NAME_MASK  (DIR_ATTR_ARCHIVE | DIR_ATTR_DIRECTORY | LDIR_ATTR_LONG_NAME)



static inline u8 calcLdirChksum(const char *shortName)
{
	u8 chksum = 0;
	for(unsigned i = 0; i < 11; i++)
	{
		chksum = ((chksum & 1u ? 0x80u : 0u) | chksum>>1) + *shortName++;
	}

	return chksum;
}

u32 makeVolId(void);
void calcFormatFat(FormatParams &params);
void calcFormatFat32(FormatParams &params);
int makeFsFat(const FormatParams &params, BufferedFsWriter &dev, const std::string &label);

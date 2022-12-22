#pragma once

#include <cstddef>
#include "types.h"

// References:
// exFAT: https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification


// Boot Sector.
typedef struct
{
	u8 jumpBoot[3];                  // {0xEB, 0x76, 0x90}.
	char fileSystemName[8];          // "EXFAT   ".
	u8 mustBeZero[53];
	u64 partitionOffset;             // Arbitrary value or 0 = ignore this field.
	u64 volumeLength;                // Minimum "2^20 / 2^BytesPerSectorShift", maximum "2^64 - 1". If excess space sub-region size = 0 then max. is "ClusterHeapOffset + (2^32 - 11) * 2^SectorsPerClusterShift".
	u32 fatOffset;                   // Minimum "24", maximum "ClusterHeapOffset - (FatLength * NumberOfFats)".
	u32 fatLength;                   // Minimum "(ClusterCount + 2) * 2^2 / 2^BytesPerSectorShift" rounded up to nearest integer, maximum "(ClusterHeapOffset - FatOffset) / NumberOfFats" rounded down to nearest integer.
	u32 clusterHeapOffset;           // Minimum "FatOffset + FatLength * NumberOfFats", maximum "2^32 - 1" or "VolumeLength - (ClusterCount * 2^SectorsPerClusterShift)" whichever is smaller.
	u32 clusterCount;                // Shall be the lesser of "(VolumeLength - ClusterHeapOffset) / 2^SectorsPerClusterShiftrounded down to the nearest integer" or "2^32 - 11". Recommended no more than "2^24 - 2".
	u32 firstClusterOfRootDirectory; // Minimum "2", maximum "ClusterCount + 1".
	u32 volumeSerialNumber;          // Volume serial number generated from date and time.
	u16 fileSystemRevision;          // versionHigh<<8 | (u8)versionLow. Version high 1-99, versionLow 0-99. Usually 1.00.
	u16 volumeFlags;                 // Bit 0 = activeFat, bit 1 = volumeDirty, bit 2 = mediaFailure, bit 3 = clearToZero, bits 4-15 reserved.
	u8  bytesPerSectorShift;         // Minimum "9" (512 bytes), maximum "12" (4 KiB).
	u8  sectorsPerClusterShift;      // Minimum "0" (1 sector), maximum "25 - BytesPerSectorShift" (32 MiB).
	u8  numberOfFats;                // "1" or "2" (TexFAT only).
	u8  driveSelect;                 // Arbitrary value. Recommended 0x80.
	u8  percentInUse;                // 0-100 "percentage of allocated clusters in the Cluster Heap, rounded down to the nearest integer" or 0xFF if unknown.
	u8 reserved[7];
	u8 bootCode[390];                // Bootstrapping code or 0xF4 filled (x86 halt).
	u16 bootSignature;               // 0xAA55.
	//u8 excessSpace[(1u<<bytesPerSectorShift) - 512];
} BootSector;
static_assert(offsetof(BootSector, bootSignature) == 510, "Member bootSignature of BootSector not at offsetof 510.");

// Extended Boot Sector.
/*typedef struct
{
	u8 extendedBootCode[(1u<<bytesPerSectorShift) - 4];
	u32 extendedBootSignature;                          // 0xAA550000.
} ExtendedBootSector;*/

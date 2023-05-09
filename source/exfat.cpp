// SPDX-License-Identifier: MIT
// Copyright (c) 2023 profi200

#include <cstdio>
#include <cstring>
#include <memory>
#include "types.h"
#include "exfat.h"
#include "exfat_up_case_table.h"
#include "fat.h" // makeVolId().
#include "util.h"



void calcFormatExFat(FormatParams &params)
{
	const u32 alignment = params.alignment;
	const u64 totSec    = params.totSec;
	params.partitionOffset   = alignment;
	params.volumeLength      = totSec - alignment;
	params.fatOffset         = alignment / 2;
	params.fatLength         = alignment / 2;
	params.clusterHeapOffset = alignment;
	params.clusterCount      = (totSec - alignment * 2) / params.secPerClus;
}

static u32 calcExFatBootChecksum(const u8 *data, const u16 bytesPerSector)
{
	u32 checksum = 0;
	for(unsigned i = 0; i < (bytesPerSector * 11); i++)
	{
		// Don't checksum volumeFlags and percentInUse.
		if(i == 106 || i == 107 || i == 112) continue;

		checksum = (checksum & 1u ? 0x80000000u : 0u) + (checksum>>1) + data[i];
	}

	return checksum;
}

// Warning, this function relies on the current buffer position in dev!
// Length must be >=1.
static int writeContinuousExfatChain(BufferedFsWriter &dev, const u32 start, const u32 length)
{
	for(u32 i = start + 1; i < start + length; i++)
	{
		const int res = dev.write(&i, 4);
		if(res != 0) return res;
	}

	const u32 eof = EXFAT_EOF;
	return dev.write(&eof, 4);
}

// Warning, this function relies on the current buffer position in dev!
// Warning, this breaks horribly on multiple calls with counts not a multiple of 32!
// Count must be >=1.
static int writeInitialBitmapEntries(BufferedFsWriter &dev, u32 count)
{
	do
	{
		const u32 bits = (count > 32 ? 32 : count);
		const u32 bitmap = 0xFFFFFFFFu>>(32u - bits);
		const int res = dev.write(&bitmap, 4);
		if(res != 0) return res;

		count -= bits;
	} while(count);

	return 0;
}

int makeFsExFat(const FormatParams &params, BufferedFsWriter &dev, const std::u16string &label)
{
	// Seek ahead to partition start and fill everything inbetween with zeros.
	const u64 partitionOffset = params.partitionOffset;
	const u16 bytesPerSec     = params.bytesPerSec;
	u64 curOffset = partitionOffset * bytesPerSec;
	int res = dev.fill(curOffset);
	if(res != 0) return res;

	// We need this buffer of 12 sectors for checksum calculation.
	const std::unique_ptr<u8[]> bootRegion(new(std::nothrow) u8[12 * bytesPerSec]{});
	if(!bootRegion) return ENOMEM;

	// ----------------------------------------------------------------
	// Boot Sector.
	const u32 secPerClus        = params.secPerClus;
	const u32 bytesPerClus      = secPerClus * bytesPerSec;
	const u32 bitsPerClus       = bytesPerClus * 8;
	const u32 clusterCount      = params.clusterCount;
	const u32 bitmapClus        = util::udivCeil(clusterCount, bitsPerClus);
	const u32 upCaseClus        = util::udivCeil(sizeof(g_upCaseTable), bytesPerClus);
	const u32 fatOffset         = params.fatOffset;
	const u32 clusterHeapOffset = params.clusterHeapOffset;
	ExfatBootSec *const bs = reinterpret_cast<ExfatBootSec*>(bootRegion.get());
	memcpy(bs->jumpBoot, BS_JUMP_BOOT, 3);
	memcpy(bs->fileSystemName, BS_FILE_SYS_NAME, 8);
	bs->partitionOffset             = partitionOffset;
	bs->volumeLength                = params.volumeLength;
	bs->fatOffset                   = fatOffset;
	bs->fatLength                   = params.fatLength;
	bs->clusterHeapOffset           = clusterHeapOffset;
	bs->clusterCount                = clusterCount;
	bs->firstClusterOfRootDirectory = EXFAT_FIRST_ENT + bitmapClus + upCaseClus;
	bs->volumeSerialNumber          = makeVolId();
	bs->fileSystemRevision          = BS_FILE_SYS_REV_1_00;
	// volumeFlags cleared to zero.
	bs->bytesPerSectorShift         = util::countTrailingZeros(bytesPerSec);
	bs->sectorsPerClusterShift      = util::countTrailingZeros(secPerClus);
	bs->numberOfFats                = 1;
	bs->driveSelect                 = BS_DRIVE_SELECT;
	// percentInUse cleared to zero.
	memset(bs->bootCode, 0xF4, sizeof(bs->bootCode)); // Fill with x86 hlt instructions.
	bs->bootSignature               = BS_BOOT_SIG;

	// ----------------------------------------------------------------
	// Extended Boot Sectors.
	// Unused. We just set the correct signatures.
	for(unsigned i = 1; i < 9; i++)
		*reinterpret_cast<u32*>(&bootRegion[bytesPerSec * i + bytesPerSec - 4]) = EBS_EXT_BOOT_SIG;

	// ----------------------------------------------------------------
	// OEM Parameters.
	// TODO: If available get this from existing exFAT volume as per spec.
	FlashParameters *const flashParams = reinterpret_cast<FlashParameters*>(&bootRegion[bytesPerSec * 9]);
	memcpy(flashParams->guid, OEM_FLASH_PARAMS_GUID, 16);
	flashParams->eraseBlockSize = params.alignment * bytesPerSec / 2;
	// All other fields are zero for SD cards.

	// ----------------------------------------------------------------
	// Boot Checksum.
	const u32 bootChecksum = calcExFatBootChecksum(bootRegion.get(), bytesPerSec);
	for(unsigned i = 0; i < bytesPerSec / 4; i++)
		*reinterpret_cast<u32*>(&bootRegion[bytesPerSec * 11 + i * 4]) = bootChecksum;

	// Write main boot region.
	res = dev.write(bootRegion.get(), bytesPerSec * 12);
	if(res != 0) return res;

	// Write backup boot region.
	res = dev.write(bootRegion.get(), bytesPerSec * 12);
	if(res != 0) return res;

	// ----------------------------------------------------------------
	// File Allocation Table.
	// Note: We need "clusterCount + 2" number of entries.
	// Note: SDFormatter does not clear the area between last FAT entry and cluster heap start.
	// TODO: Should we set unused entries to reserved/eof? If we do also set the bitmap bits.
	// First (reserved) + second (EOF) entry.
	curOffset += fatOffset * bytesPerSec;
	const u32 reservedEnt[2] = {EXFAT_RESERVED, EXFAT_EOF};
	res = dev.fillAndWrite(reservedEnt, curOffset, 8);
	if(res != 0) return res;

	// Bitmap cluster chain.
	res = writeContinuousExfatChain(dev, 0, bitmapClus);
	if(res != 0) return res;

	// Up-case Table cluster chain.
	res = writeContinuousExfatChain(dev, bitmapClus, upCaseClus);
	if(res != 0) return res;

	// Root directory cluster chain.
	// TODO: Is 1 cluster always safe for root dir? Minimum size is 512 bytes.
	res = writeContinuousExfatChain(dev, bitmapClus + upCaseClus, 1);
	if(res != 0) return res;

	// ----------------------------------------------------------------
	// Allocation Bitmap.
	curOffset = (partitionOffset + clusterHeapOffset) * bytesPerSec;
	res = dev.fill(curOffset);
	if(res != 0) return res;

	// TODO: Is 1 cluster always safe for root dir? Minimum size is 512 bytes.
	res = writeInitialBitmapEntries(dev, bitmapClus + upCaseClus + 1);
	if(res != 0) return res;

	// ----------------------------------------------------------------
	// Up-case Table.
	curOffset += secPerClus * bytesPerSec * bitmapClus;
	res = dev.fillAndWrite(g_upCaseTable, curOffset, sizeof(g_upCaseTable));
	if(res != 0) return res;

	// ----------------------------------------------------------------
	// Root Directory.
	// We always include a label entry even if label size is 0. This is allowed by the exFAT spec.
	ExfatDirEnt entries[3]{};
	entries[0].entryType            = TYPE_VOL_LABEL;
	entries[0].label.characterCount = label.length();
	memcpy(entries[0].label.volumeLabel, label.c_str(), sizeof(char16_t) * label.length());

	entries[1].entryType           = TYPE_BITMAP;
	// entries[1].bitmap.flags cleared to zero.
	entries[1].bitmap.firstCluster = EXFAT_FIRST_ENT;
	entries[1].bitmap.dataLength   = util::udivCeil(clusterCount, 8u);

	entries[2].entryType            = TYPE_UP_CASE;
	entries[2].upCase.tableChecksum = UP_CASE_TABLE_CHECKSUM;
	entries[2].upCase.firstCluster  = EXFAT_FIRST_ENT + bitmapClus;
	entries[2].upCase.dataLength    = sizeof(g_upCaseTable);

	curOffset += secPerClus * bytesPerSec * upCaseClus;
	res = dev.fillAndWrite(entries, curOffset, sizeof(entries));
	if(res != 0) return res;

	// Fill remaining directory entries.
	curOffset += secPerClus * bytesPerSec;
	return dev.fill(curOffset);
}

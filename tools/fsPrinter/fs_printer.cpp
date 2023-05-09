// SPDX-License-Identifier: MIT
// Copyright (c) 2023 profi200

#define _FILE_OFFSET_BITS 64
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include "../../include/mbr.h"
#include "../../include/fat.h"
#include "../../include/exfat.h"


static int g_fd = -1;



static int openFile(const char *const path)
{
	const int fd = open(path, O_RDONLY);
	if(fd != -1)
	{
		g_fd = fd;
		return 0;
	}

	const int res = errno;
	perror("Failed to open file");
	return res;
}

// TODO: Support for large sectors.
static int readSectors(void *buf, const u64 sector, const u64 count)
{
	int res = 0;
	u8 *_buf = reinterpret_cast<u8*>(buf);
	off_t offset = sector * 512;
	u64 totSize = count * 512;
	while(totSize > 0)
	{
		// Limit of 1 GiB chunks.
		const size_t blkSize = (totSize > 0x40000000 ? 0x40000000 : totSize);
		const ssize_t _read = ::pread(g_fd, _buf, blkSize, offset);
		if(_read == -1)
		{
			res = errno;
			break;
		}

		_buf += _read;
		offset += _read;
		totSize -= _read;
	}

	if(res != 0) perror("Failed to read sectors");
	return res;
}

static void closeFile(void)
{
	while(close(g_fd) == -1 && errno == EINTR);
	g_fd = -1;
}

static void hexdump(const char *const indent, const u8 *data, const u32 len)
{
	u32 printed = 0;
	while(printed < len)
	{
		printf("%s%08" PRIX32 ":  ", indent, printed);

		u32 bytesToPrint = (len - printed > 16 ? 16 : len - printed);
		do
		{
			printf("%02" PRIX8 " ", *data++);
		} while(--bytesToPrint);
		puts("");

		printed += 16;
	}
}

static void printGuid(const u8 *const guid)
{
	printf("{%08" PRIX32 "-%04" PRIX16 "-%04" PRIX16 "-%04" PRIX16 "-",
	       *(u32*)&guid[0], *(u16*)&guid[4], *(u16*)&guid[6], __builtin_bswap16(*(u16*)&guid[8]));
	for(u32 i = 0; i < 6; i++) printf("%02" PRIX8, guid[10 + i]);
	printf("}");
}

static int printFat(u32 partStartLba)
{
	BootSec bs;
	int res = readSectors(&bs, partStartLba, 1);
	if(res != 0) return res;

	// Adjust for logical sectors.
	partStartLba /= bs.bytesPerSec>>9;

	printf("Boot Sector (FAT):\n"
	       "\tjmp instruction:            0x%02" PRIX8 " 0x%02" PRIX8 " 0x%02" PRIX8 "\n"
	       "\tOEM name:                   \"%.8s\"\n\n"
	       "BIOS Parameter Block:\n"
	       "\tBytes per sector:           %" PRIu16 "\n"
	       "\tSectors per cluster:        %" PRIu8 " (%" PRIu32 " KiB)\n"
	       "\tReserved sectors:           %" PRIu16 "\n"
	       "\tFATs:                       %" PRIu8 "\n"
	       "\tRoot directory entries:     %" PRIu16 "\n"
	       "\tTotal sectors (16 bit):     %" PRIu16 "\n"
	       "\tMedia:                      0x%02" PRIX8 "\n"
	       "\tSectors per FAT (FAT12/16): %" PRIu16 "\n"
	       "\tSectors per track:          %" PRIu16 "\n"
	       "\tHeads:                      %" PRIu16 "\n"
	       "\tHidden sectors:             %" PRIu32 "\n"
	       "\tTotal sectors (32 bit):     %" PRIu32 "\n\n",
	       bs.jmpBoot[0], bs.jmpBoot[1], bs.jmpBoot[2],
	       bs.oemName,
	       bs.bytesPerSec,
	       bs.secPerClus, (u32)bs.bytesPerSec * bs.secPerClus / 1024,
	       bs.rsvdSecCnt,
	       bs.numFats,
	       bs.rootEntCnt,
	       bs.totSec16,
	       bs.media,
	       bs.fatSz16,
	       bs.secPerTrk,
	       bs.numHeads,
	       bs.hiddSec,
	       bs.totSec32);

	// Not the recommended way of differntiating between FAT12/16 and FAT32
	// but fastfat.sys and msdos.ko/vfat.ko do it like this.
	const bool isFat32 = bs.fatSz16 == 0;
	if(!isFat32) // FAT12/FAT16
	{
		printf("Extended BIOS Parameter Block:\n"
		       "\tDrive number:               0x%02" PRIX8 "\n"
		       "\tReserved 1:                 0x%02" PRIX8 "\n"
		       "\tExtended boot signature:    0x%02" PRIX8 "\n"
		       "\tVolume ID:                  0x%08" PRIX32 "\n"
		       "\tVolume label:               \"%.11s\"\n"
		       "\tFS type:                    \"%.8s\"\n",
		       bs.ebpb.drvNum,
		       bs.ebpb.reserved1,
		       bs.ebpb.bootSig,
		       bs.ebpb.volId,
		       bs.ebpb.volLab,
		       bs.ebpb.filSysType);
		puts("\tBoot code:");
		hexdump("\t\t", bs.ebpb.bootCode, sizeof(bs.ebpb.bootCode));
	}
	else // FAT32
	{
		printf("Extended BIOS Parameter Block:\n"
		       "\tSectors per FAT (FAT32):    %" PRIu32 "\n"
		       "\tFlags:                      0x%04" PRIX16 "\n"
		       "\tFAT version:                %" PRIu8 ".%" PRIu8 "\n"
		       "\tRoot directory cluster:     %" PRIu32 "\n"
		       "\tFSInfo sector:              %" PRIu16 "\n"
		       "\tBackup boot sector:         %" PRIu16 "\n"
		       "\tDrive number:               0x%02" PRIX8 "\n"
		       "\tReserved 1:                 0x%02" PRIX8 "\n"
		       "\tExtended boot signature:    0x%02" PRIX8 "\n"
		       "\tVolume ID:                  0x%08" PRIX32 "\n"
		       "\tVolume label:               \"%.11s\"\n"
		       "\tFS type:                    \"%.8s\"\n",
		       bs.ebpb32.fatSz32,
		       bs.ebpb32.extFlags,
		       bs.ebpb32.fsVer>>8, bs.ebpb32.fsVer & 0xFF,
		       bs.ebpb32.rootClus,
		       bs.ebpb32.fsInfoSector,
		       bs.ebpb32.bkBootSec,
		       bs.ebpb32.drvNum,
		       bs.ebpb32.reserved1,
		       bs.ebpb32.bootSig,
		       bs.ebpb32.volId,
		       bs.ebpb32.volLab,
		       bs.ebpb32.filSysType);
		puts("\tBoot code:");
		hexdump("\t\t", bs.ebpb32.bootCode, sizeof(bs.ebpb32.bootCode));
	}
	printf("\tSignature word:             0x%04" PRIX16 "\n\n", bs.sigWord);

	if(isFat32)
	{
		FsInfo fsInfo;
		res = readSectors(&fsInfo, (partStartLba + bs.ebpb32.fsInfoSector) * bs.bytesPerSec>>9, 1);
		if(res != 0)
		{
			fputs("Could not read FSInfo sector.\n", stderr);
		}
		else
		{
			printf("FSInfo sector:\n"
			       "\tLeading signature:  0x%08" PRIX32 "\n"
			       "\tStruct signature:   0x%08" PRIX32 "\n"
			       "\tFree count:         %" PRIu32 "\n"
			       "\tNext free:          %" PRIu32 "\n"
			       "\tTrailing signature: 0x%08" PRIX32 "\n\n",
			       fsInfo.leadSig,
			       fsInfo.strucSig,
			       fsInfo.freeCount,
			       fsInfo.nxtFree,
			       fsInfo.trailSig);
		}
	}

	const u32 fatSize = (isFat32 ? bs.ebpb32.fatSz32 : bs.fatSz16);
	const u32 rootDirSectors = ((32 * bs.rootEntCnt) + (bs.bytesPerSec - 1)) / bs.bytesPerSec;
	const u32 dataStart = bs.rsvdSecCnt + (fatSize * bs.numFats) + rootDirSectors;
	printf("First FAT at %" PRIu32 " (absolute %" PRIu32 ").\n"
	       "Second FAT at %" PRIu32 " (absolute %" PRIu32 ").\n"
	       "Root directory at %" PRIu32 " (absolute %" PRIu32 ").\n"
	       "Data area at %" PRIu32 " (absolute %" PRIu32 ").\n",
	       bs.rsvdSecCnt, partStartLba + bs.rsvdSecCnt,
	       bs.rsvdSecCnt + fatSize, partStartLba + bs.rsvdSecCnt + fatSize,
	       dataStart - rootDirSectors, partStartLba + (dataStart - rootDirSectors),
	       dataStart, partStartLba + dataStart);

	return 0;
}

int printExfat(const u64 partStartLba)
{
	ExfatBootSec bs;
	int res = readSectors(&bs, partStartLba, 1);
	if(res != 0) return res;

	// Adjust for logical sectors.
	//partStartLba >>= bs.bytesPerSectorShift - 9; // bytesPerSectorShift is minimum 9.

	printf("Boot Sector (exFAT):\n"
	       "\tjmp instruction:           0x%02" PRIX8 " 0x%02" PRIX8 " 0x%02" PRIX8 "\n"
	       "\tFilesystem name:           \"%.8s\"\n"
	       "\tPartition offset:          %" PRIu64 "\n"
	       "\tVolume length:             %" PRIu64 "\n"
	       "\tFAT offset:                %" PRIu32 "\n"
	       "\tFAT length:                %" PRIu32 "\n"
	       "\tCluster heap offset:       %" PRIu32 "\n"
	       "\tCluster count:             %" PRIu32 "\n"
	       "\tFirst root dir cluster:    %" PRIu32 "\n"
	       "\tVolume serial number:      0x%08" PRIX32 "\n"
	       "\tFilesystem revision:       %" PRIu8 ".%" PRIu8 "\n"
	       "\tVolume flags:              0x%04" PRIX16 "\n"
	       "\tBytes per sector shift:    %" PRIu8 "\n"
	       "\tSectors per cluster shift: %" PRIu8 "\n"
	       "\tNumber of FATs:            %" PRIu8 "\n"
	       "\tDrive select:              0x%02" PRIX8 "\n"
	       "\tPercent in use:            %" PRIu8 "\n",
	       bs.jumpBoot[0], bs.jumpBoot[1], bs.jumpBoot[2],
	       bs.fileSystemName,
	       bs.partitionOffset,
	       bs.volumeLength,
	       bs.fatOffset,
	       bs.fatLength,
	       bs.clusterHeapOffset,
	       bs.clusterCount,
	       bs.firstClusterOfRootDirectory,
	       bs.volumeSerialNumber,
	       bs.fileSystemRevision>>8, bs.fileSystemRevision & 0xFF,
	       bs.volumeFlags,
	       bs.bytesPerSectorShift,
	       bs.sectorsPerClusterShift,
	       bs.numberOfFats,
	       bs.driveSelect,
	       bs.percentInUse);
	puts("\tBoot code:");
	hexdump("\t\t", bs.bootCode, sizeof(bs.bootCode));
	printf("\tBoot signature:            0x%04" PRIX16 "\n", bs.bootSignature);

	u8 oemParameters[512]; // TODO: Support for other sector sizes.
	res = readSectors(oemParameters, partStartLba + 9, 1);
	if(res != 0) return res;

	for(u32 i = 0; i < 10; i++)
	{
		const u8 *const params = &oemParameters[48 * i];

		// Check GUID.
		static const u8 nullGuid[16] = {0};
		if(memcmp(params, nullGuid, 16) == 0) continue;

		// Flash parameters.
		static const u8 flashGuid[16] = {0x46, 0x7E, 0x0C, 0x0A, 0x99, 0x33, 0x21, 0x40, 0x90, 0xC8, 0xFA, 0x6D, 0x38, 0x9C, 0x4B, 0xA2};
		if(memcmp(params, flashGuid, 16) == 0)
		{
			const FlashParameters *const flashParams = (const FlashParameters*)params;
			printf("\nOEM flash parameters:\n"
			       "\tGUID:               {0A0C7E46-3399-4021-90C8-FA6D389C4BA2}\n"
			       "\tErase block size:   0x%08" PRIX32 "\n"
			       "\tPage size:          0x%08" PRIX32 "\n"
			       "\tSpare sectors:      %" PRIu32 "\n"
			       "\tRandom access time: %" PRIu32 " ns\n"
			       "\tProgramming time:   %" PRIu32 " ns\n"
			       "\tRead cycle:         %" PRIu32 " ns\n"
			       "\tWrite cycle:        %" PRIu32 " ns\n",
			       flashParams->eraseBlockSize,
			       flashParams->pageSize,
			       flashParams->spareSectors,
			       flashParams->randomAccessTime,
			       flashParams->programmingTime,
			       flashParams->readCycle,
			       flashParams->writeCycle);
		}
		else
		{
			printf("\nUnknown OEM parameters:\n\tGUID: ");
			printGuid(params);
			puts("");
			hexdump("\t", &params[16], 32);
		}
	}

	return 0;
}

// TODO: Detect when there is no partition table but just a FS. Also GPT support.
int printDiskInfo(const char *const path)
{
	int res = openFile(path);
	Mbr mbr;
	if(res == 0)
	{
		res = readSectors(&mbr, 0, 1);
		if(res != 0) closeFile();
	}
	if(res != 0) return res;

	puts("Master Boot Record:\n\tBootstrap code:");
	hexdump("\t\t", mbr.bootstrap, sizeof(mbr.bootstrap));
	printf("\tDisk signature: 0x%08" PRIX32 "\n"
	       "\tReserved:       0x%04" PRIX16 "\n",
	       mbr.diskSig,
	       mbr.reserved);

	for(u32 i = 0; i < 4; i++)
	{
		const PartEntry &entry = mbr.partTable[i];
		if(entry.startLBA == 0 || entry.sectors == 0) continue;

		printf("\tPartition %" PRIu32 ":\n"
		       "\t\tStatus:         0x%02" PRIX8 "\n"
		       "\t\tStart head:     %" PRIu8 "\n"
		       "\t\tStart sector:   %" PRIu16 "\n"
		       "\t\tStart cylinder: %" PRIu16 "\n"
		       "\t\tType:           0x%02" PRIX8 "\n"
		       "\t\tEnd head:       %" PRIu8 "\n"
		       "\t\tEnd sector:     %" PRIu16 "\n"
		       "\t\tEnd cylinder:   %" PRIu16 "\n"
		       "\t\tStart LBA:      %" PRIu32 "\n"
		       "\t\tSectors:        %" PRIu32 "\n",
		       i + 1,
		       entry.status,
		       entry.startCHS[0],
		       entry.startCHS[1] & 0x3F,
		       (u16)(entry.startCHS[1] & 0xC0)<<2 | entry.startCHS[2],
		       entry.type,
		       entry.endCHS[0],
		       entry.endCHS[1] & 0x3F,
		       (u16)(entry.endCHS[1] & 0xC0)<<2 | entry.endCHS[2],
		       entry.startLBA,
		       entry.sectors);
	}

	for(u32 i = 0; i < 4; i++)
	{
		const PartEntry &entry = mbr.partTable[i];
		if(entry.startLBA == 0 || entry.sectors == 0) continue;

		printf("\nPartition %" PRIu32 " filesystem:\n", i + 1);

		switch(entry.type)
		{
			case 0x01:
			case 0x04:
			case 0x06:
			case 0x0B:
			case 0x0C:
				res = printFat(entry.startLBA);
				break;
			case 0x07:
				res = printExfat(entry.startLBA);
				break;
			default:
				puts("Unknown filesystem."); // FS not supported.
		}
	}

	closeFile();

	return res;
}

int main(const int argc, char *const argv[])
{
	if(argc != 2)
	{
		puts("Usage: fsPrinter DEVICE");
		return EINVAL;
	}

	return printDiskInfo(argv[1]);
}

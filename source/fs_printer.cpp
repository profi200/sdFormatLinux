#include <cstdio>
#include "mbr.h"
#include "exfat.h"
#include "fat.h"



/*static void hexdump(const u8 *data, const u32 len)
{
	u32 printed = 0;
	while(printed < len)
	{
		printf("%08" PRIX32 ":  ", printed);

		u32 bytesToPrint = (len - printed > 16 ? 16 : len - printed);
		do
		{
			printf("%02" PRIX8 " ", *data++);
		} while(--bytesToPrint);
		puts("");

		printed += 16;
	}
}*/

// TODO: Rewrite to use less args for printf().
static void printVbr(const Vbr *vbr, const u32 partStartLba)
{
	printf("Volume Boot Record:\n"
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
	       vbr->jmpBoot[0], vbr->jmpBoot[1], vbr->jmpBoot[2],
	       vbr->oemName,
	       vbr->bytesPerSec,
	       vbr->secPerClus, (u32)vbr->bytesPerSec * vbr->secPerClus / 1024,
	       vbr->rsvdSecCnt,
	       vbr->numFats,
	       vbr->rootEntCnt,
	       vbr->totSec16,
	       vbr->media,
	       vbr->fatSz16,
	       vbr->secPerTrk,
	       vbr->numHeads,
	       vbr->hiddSec,
	       vbr->totSec32);

	// TODO: This is not the recommended way of differntiating between FAT12/16 and FAT32.
	const bool isFat32 = vbr->rootEntCnt == 0;
	if(!isFat32) // FAT12/FAT16
	{
		printf("Extended BIOS Parameter Block:\n"
		       "\tDrive number:               0x%02" PRIX8 "\n"
		       "\tReserved 1:                 0x%02" PRIX8 "\n"
		       "\tExtended boot signature:    0x%02" PRIX8 "\n"
		       "\tVolume ID:                  0x%08" PRIX32 "\n"
		       "\tVolume label:               \"%.11s\"\n"
		       "\tFS type:                    \"%.8s\"\n",
		       vbr->fat16.drvNum,
		       vbr->fat16.reserved1,
		       vbr->fat16.bootSig,
		       vbr->fat16.volId,
		       vbr->fat16.volLab,
		       vbr->fat16.filSysType);

		// TODO: Separate option to print this?
		//hexdump(vbr->fat16.bootCode, sizeof(vbr->fat16.bootCode));
	}
	else // FAT32
	{
		// TODO: Test this.
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
		       vbr->fat32.fatSz32,
		       vbr->fat32.extFlags,
		       vbr->fat32.fsVer>>8, vbr->fat32.fsVer & 0xFF,
		       vbr->fat32.rootClus,
		       vbr->fat32.fsInfoSector,
		       vbr->fat32.bkBootSec,
		       vbr->fat32.drvNum,
		       vbr->fat32.reserved1,
		       vbr->fat32.bootSig,
		       vbr->fat32.volId,
		       vbr->fat32.volLab,
		       vbr->fat32.filSysType);

		// TODO: Separate option to print this?
		//hexdump(vbr->fat32.bootCode, sizeof(vbr->fat32.bootCode));

		// TODO: FSInfo.
	}

	printf("\tSignature word:             0x%04" PRIX16 "\n\n", vbr->sigWord);

	const u32 fatSize = (isFat32 ? vbr->fat32.fatSz32 : vbr->fatSz16);
	const u32 rootDirSectors = ((32 * vbr->rootEntCnt) + (vbr->bytesPerSec - 1)) / vbr->bytesPerSec;
	const u32 dataStart = vbr->rsvdSecCnt + (fatSize * vbr->numFats) + rootDirSectors;
	printf("First FAT at %" PRIu32 " (%" PRIu32 ").\n"
	       "Second FAT at %" PRIu32 " (%" PRIu32 ").\n"
	       "Root directory at %" PRIu32 " (%" PRIu32 ").\n"
	       "Data area at %" PRIu32 " (%" PRIu32 ").\n",
	       vbr->rsvdSecCnt, partStartLba + vbr->rsvdSecCnt,
	       vbr->rsvdSecCnt + fatSize, partStartLba + vbr->rsvdSecCnt + fatSize,
	       dataStart - rootDirSectors, partStartLba + (dataStart - rootDirSectors),
	       dataStart, partStartLba + dataStart);
}

// TODO: Rewrite to use less args for printf().
int printDiskInfo(const char *const path)
{
	int res = 0;
	FILE *const f = fopen(path, "rb"); // TODO: Use open()/read()/close()?
	Mbr mbr{};
	if(f != NULL)
	{
		if(fread(&mbr, sizeof(Mbr), 1, f) != 1)
		{
			res = 3;
			fputs("Failed to read file.", stderr);
		}
	}
	else
	{
		res = 2;
		fputs("Failed to open file.", stderr);
	}
	if(res != 0) return res;

	puts("Master Boot Record:");
	//hexdump(mbr.code, sizeof(mbr.code)); // TODO: Separate option to print this?
	printf("\tDisk signature:  0x%08" PRIX32 "\n"
	       "\tReserved:        0x%04" PRIX16 "\n"
	       "\tBoot signature:  0x%04" PRIX16 "\n\n",
	       mbr.diskSig,
	       mbr.reserved,
	       mbr.bootSig);

	for(u32 i = 0; i < 4; i++)
	{
		const PartEntry &entry = mbr.partTable[i];
		if(entry.startLBA == 0 || entry.sectors == 0) continue;

		printf("Partition %" PRIu32 " info:\n"
		       "\tStatus:                0x%02" PRIX8 "\n"
		       "\tStart head:            %" PRIu8 "\n"
		       "\tStart sector:          %" PRIu16 "\n"
		       "\tStart cylinder:        %" PRIu16 "\n"
		       "\tType:                  0x%02" PRIX8 "\n"
		       "\tEnd head:              %" PRIu8 "\n"
		       "\tEnd sector:            %" PRIu16 "\n"
		       "\tEnd cylinder:          %" PRIu16 "\n"
		       "\tStart LBA:             %" PRIu32 "\n"
		       "\tSectors:               %" PRIu32 "\n\n",
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

		if(fseek(f, 512 * entry.startLBA, SEEK_SET) == 0)
		{
			Vbr br{};
			if(fread(&br, sizeof(Vbr), 1, f) != 1)
			{
				res = 3;
				break;
			}
			printVbr(&br, entry.startLBA);
		}
		else
		{
			res = 4;
			break;
		}
	}

	fclose(f);

	return res;
}

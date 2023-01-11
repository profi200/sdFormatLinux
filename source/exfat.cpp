#include <cstdio>
#include "types.h"
#include "exfat.h"



u32 calcExFatBootChecksum(const u8 *data, const u16 bytesPerSector)
{
	u32 checksum = 0;
	for(u32 i = 0; i < (bytesPerSector * 11); i++)
	{
		if(i == 106 || i == 107 || i == 112) continue;

		checksum = (checksum & 1u ? 0x80000000 : 0) + (checksum>>1) + data[i];
	}

	return checksum;
}

void calcFormatExFat(void) // (const u64 totSec, FormatParams *const paramsOut)
{
	// TODO
	fputs("exFAT is not supported yet.\n", stderr);
}

/*static void makeFsExFat(const FormatParams &params, BufferedFsWriter &dev, const char *const label)
{
	// Volume Boot Record (VBR).
	// TODO
}*/

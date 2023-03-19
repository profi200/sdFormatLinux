// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstring>
#include <sys/random.h> // getrandom()...
#include "types.h"
#include "mbr.h"
#include "format.h"
#include "verbose_printf.h"



// Converts LBA to MBR CHS format.
static u32 lba2chs(u64 lba, const u32 heads, const u32 secPerTrk)
{
	const u32 spc = heads * secPerTrk;
	u32 c = lba / spc;
	lba %= spc;
	u32 h = lba / secPerTrk;
	u32 s = lba % secPerTrk + 1;

	// Return the maximum if we can't encode this.
	if(c >= 1024 || h >= heads || s > secPerTrk)
	{
		c = 1023;
		h = 254;
		s = 63;
	}

	return (c & 0xFFu)<<16 | (c & 0x300u)<<6 | s<<8 | h;
}

// TODO: Rewrite this function to use partSize to determine the fs type.
int createMbrAndPartition(const FormatParams &params, BufferedFsWriter &dev)
{
	// Master Boot Record (MBR).
	// Generate a new, random disk signature.
	// TODO: If getrandom() returns -1 abort. We should probably make a wrapper.
	Mbr mbr{};
	while(getrandom(&mbr.diskSig, 4, 0) != 4);
	verbosePrintf("Disk ID: 0x%08" PRIX32 "\n", mbr.diskSig);

	// Set partition to inactive.
	PartEntry &entry = mbr.partTable[0];
	entry.status = 0x00;

	// Set start C/H/S.
	const u32 bytesPerSec = params.bytesPerSec;
	const u32 partStart   = params.partStart * (bytesPerSec>>9); // Convert back to physical sectors.
	const u32 heads       = params.heads;
	const u32 secPerTrk   = params.secPerTrk;
	const u32 startCHS    = lba2chs(partStart, heads, secPerTrk);
	memcpy(entry.startCHS, &startCHS, 3);

	// Set partition filesystem type.
	const u8 fatBits   = params.fatBits;
	const u64 totSec   = params.totSec * (bytesPerSec>>9); // Convert back to physical sectors.
	const u64 partSize = totSec - partStart; // TODO: Is this correct or should we align the end?
	u8 type;
	if(fatBits == 12)          type = 0x01; // FAT12 (16/32 MiB).
	else if(fatBits == 16)
	{
		if(partSize < 65536)   type = 0x04; // FAT16 (<32 MiB).
		else                   type = 0x06; // FAT16 (>=32 MiB).
	}
	else if(fatBits == 32)
	{
		if(totSec <= 16450560) type = 0x0B; // FAT32 CHS.
		else                   type = 0x0C; // FAT32 LBA.
	}
	else                       type = 0x07; // exFAT.
	entry.type = type;
	verbosePrintf("Partition type: 0x%02" PRIX8 "\n", type);

	// Set end C/H/S.
	const u32 endCHS = lba2chs(totSec - 1, heads, secPerTrk);
	memcpy(entry.endCHS, &endCHS, 3);

	// Set start LBA and number of sectors.
	entry.startLBA = partStart;
	entry.sectors  = (u32)partSize;

	// Set boot signature.
	mbr.bootSig = 0xAA55;

	// Write new MBR to card.
	return dev.write(reinterpret_cast<u8*>(&mbr), sizeof(Mbr));
}

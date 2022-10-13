#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "fs_printer.h"
#include "format.h"


#define FLAGS_FORCE_FAT32  (1u)
#define FLAGS_PRINT_FS     (1u<<1)
#define FLAGS_VERBOSE      (1u<<2)



static void printHelp(void)
{
	puts("sdFormatLinux 0.0 by profi200\n"
	     "Usage: sdFormatLinux [OPTIONS...] DEVICE\n\n"
	     "Options:\n"
	     "  -c, --capacity SECTORS   Override capacity for fake cards or image files.\n"
	     "  -d, --dry-run            Only pretend to format the card (non-destructive).\n"
	     "  -f, --force-fat32        Force format SDXC cards as FAT32.\n"
	     "                           No effect on other types of SD cards.\n"
	     "  -l, --label LABEL        Volume label. Maximum 11 upper case characters.\n"
	     "  -p, --print-fs           Don't format. Print the filesystem structure.\n"
	     "  -v, --verbose            Show format details.\n"
	     "  -h, --help               Output this help.\n");
}

int main(const int argc, char *const argv[])
{
	/*if(argc != 2)
	{
		fputs("Not enough arguments.", stderr);
		return 1;
	}*/

	//FormatParams params = {0};
	/*getAndPrintFormatParams(1u<<11, false, &params);
	getAndPrintFormatParams(1u<<12, false, &params);
	getAndPrintFormatParams(1u<<13, false, &params);
	getAndPrintFormatParams(1u<<14, false, &params);
	getAndPrintFormatParams(1u<<15, false, &params);
	getAndPrintFormatParams(1u<<16, false, &params);
	getAndPrintFormatParams(1u<<17, false, &params);
	getAndPrintFormatParams(1u<<18, false, &params);
	getAndPrintFormatParams(1u<<19, false, &params);
	getAndPrintFormatParams(1u<<20, false, &params);
	getAndPrintFormatParams(1u<<21, false, &params);
	getAndPrintFormatParams(1u<<22, false, &params);
	getAndPrintFormatParams(1u<<23, false, &params);
	getAndPrintFormatParams(1u<<24, false, &params);
	getAndPrintFormatParams(1u<<25, false, &params);
	getAndPrintFormatParams(1u<<26, false, &params);
	getAndPrintFormatParams(1u<<26, true, &params);
	getAndPrintFormatParams(1u<<27, false, &params);
	getAndPrintFormatParams(1u<<27, true, &params);
	getAndPrintFormatParams(1u<<28, false, &params);
	getAndPrintFormatParams(1u<<28, true, &params);
	getAndPrintFormatParams(1u<<29, false, &params);
	getAndPrintFormatParams(1u<<29, true, &params);
	getAndPrintFormatParams(1u<<30, false, &params);
	getAndPrintFormatParams(1u<<30, true, &params);
	getAndPrintFormatParams(1u<<31, false, &params);
	getAndPrintFormatParams(1u<<31, true, &params);*/
	//getAndPrintFormatParams(7829504, false, &params);  // Toshiba 4 GB.
	//getAndPrintFormatParams(3935232, false, &params);  // Panasonic 2 GB.
	//getAndPrintFormatParams(3862528, false, &params);  // SanDisk 2 GB.
	//getAndPrintFormatParams(31074304, false, &params); // Hama 16 GB.
	//formatSd(&params, /*"/dev/mmcblk0"*/ "test_out_img.bin", "TEST12345");


	// TODO: Dry run flag so we can test before overwriting the card.
	// TODO: Overwrite/erase format option?
	static const struct option long_options[] =
	{{   "capacity", required_argument, NULL, 'c'},
	 {"force-fat32",       no_argument, NULL, 'f'},
	 {      "label", required_argument, NULL, 'l'},
	 {   "print-fs",       no_argument, NULL, 'p'},
	 {    "verbose",       no_argument, NULL, 'v'},
	 {       "help",       no_argument, NULL, 'h'},
	 {         NULL,                 0, NULL,   0}};

	u64 overrTotSec = 0;
	u32 flags = 0;
	char label[12] = {0};
	while(1)
	{
		const int c = getopt_long(argc, argv, "c:fl:pvh", long_options, NULL);
		if(c == (-1)) break;

		switch(c)
		{
			case 'c':
				{
					overrTotSec = strtoull(optarg, NULL, 0);
					if(overrTotSec == 0 || overrTotSec > 1ull<<32) // Max 2 TiB.
					{
						fputs("Error: Image size 0 or out of range.\n", stderr);
						return 1;
					}
				}
				break;
			case 'f':
				flags |= FLAGS_FORCE_FAT32;
				break;
			case 'l':
				{
					if(strlen(optarg) > 11)
					{
						fputs("Error: Label is longer than 11 characters.\n", stderr);
						return 1;
					}
					strncpy(label, optarg, 11);
					/*for(u32 i = 0; i < 11; i++)
					{
						const unsigned char lc = label[i];
						if(lc < 0x20 || lc == 0x22 ||
						   (lc >= 0x2A && lc <= 0x2C) ||
						   lc == 0x2E || lc == 0x2F ||
						   (lc >= 0x3A && lc <= 0x3F) ||
						   (lc >= 0x5B && lc <= 0x5D) ||
						   lc == 0x7C)
						{
							fputs("Error: Label contains invalid characters.\n", stderr);
							return 1;
						}
						// TODO: The label should be encoded in the system's DOS code page. Convert from UTF-8 to DOS code page.
						// TODO: Check for uppercase chars and give a warning.
					}*/
				}
				break;
			case 'p':
				flags |= FLAGS_PRINT_FS;
				break;
			case 'v':
				flags |= FLAGS_VERBOSE;
				break;
			case 'h':
				printHelp();
				return 0;
				break;
			case '?':
				// Fallthrough.
			default:
				printHelp();
				return 1;
		}
	}

	if(argc - optind < 1 || argc - optind > 1)
	{
		printHelp();
		return 1;
	}

	const char *const devPath = argv[optind];
	if((flags & FLAGS_PRINT_FS) == 0)
	{
		setVerboseMode((flags & FLAGS_VERBOSE) != 0);
		return formatSd(devPath, (*label != 0 ? label : NULL), (flags & FLAGS_FORCE_FAT32) != 0, overrTotSec);
	}

	return printDiskInfo(devPath);
}

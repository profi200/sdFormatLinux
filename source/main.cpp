#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <getopt.h>
#include "fs_printer.h"
#include "format.h"



static void printHelp(void)
{
	puts("sdFormatLinux 0.0 by profi200\n"
	     "Usage: sdFormatLinux [OPTIONS...] DEVICE\n\n"
	     "Options:\n"
	     "  -c, --capacity SECTORS   Override capacity for fake cards or image files.\n"
	     //"  -d, --dry-run            Only pretend to format the card (non-destructive).\n"
	     "  -e, --erase TYPE         Erases the whole card before formatting (aka TRIM).\n"
	     "                           No effect with USB card readers.\n"
	     "                           TYPE can be 'trim' or 'secure'.\n"
	     "  -f, --force-fat32        Force format SDXC cards as FAT32.\n"
	     "                           No effect on other types of SD cards.\n"
	     "  -l, --label LABEL        Volume label. Maximum 11 upper case characters.\n"
	     "  -p, --print-fs           Don't format. Print the filesystem structure.\n"
	     "  -v, --verbose            Show format details.\n"
	     "  -h, --help               Output this help.\n");
}

int main(const int argc, char *const argv[])
{
	static const struct option long_options[] =
	{{   "capacity", required_argument, NULL, 'c'},
	 {    "dry-run",       no_argument, NULL, 'd'},
	 {      "erase", required_argument, NULL, 'e'},
	 {"force-fat32",       no_argument, NULL, 'f'},
	 {      "label", required_argument, NULL, 'l'},
	 {   "print-fs",       no_argument, NULL, 'p'},
	 {    "verbose",       no_argument, NULL, 'v'},
	 {       "help",       no_argument, NULL, 'h'},
	 {         NULL,                 0, NULL,   0}};

	u64 overrTotSec = 0;
	ArgFlags flags{};
	char label[12]{};
	while(1)
	{
		const int c = getopt_long(argc, argv, "c:de:fl:pvh", long_options, NULL);
		if(c == -1) break;

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
			case 'd':
				flags.dryRun = 1;
				break;
			case 'e':
				{
					if(strcmp(optarg, "trim") == 0)
						flags.erase = 1;
					else if(strcmp(optarg, "secure") == 0)
						flags.secErase = 1;
					else
					{
						fprintf(stderr, "Error: Invalid erase type '%s'.\n", optarg);
						return 1;
					}
				}
				break;
			case 'f':
				flags.forceFat32 = 1;
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
				flags.printFs = 1;
				break;
			case 'v':
				flags.verbose = 1;
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
	int res;
	try
	{
		if(!flags.printFs)
		{
			setVerboseMode(flags.verbose);
			res = formatSd(devPath, (*label != 0 ? label : NULL), flags, overrTotSec);
		}
		else res = printDiskInfo(devPath);
	}
	catch(const std::exception& e)
	{
		fprintf(stderr, "An exception occured: what(): '%s'\n", e.what());
		res = 5;
	}
	catch(...)
	{
		fprintf(stderr, "Unknown exception. Exiting...\n");
		res = 6;
	}

	return res;
}

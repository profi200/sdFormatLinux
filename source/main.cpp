// SPDX-License-Identifier: MIT

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <getopt.h>
#include "errors.h"
#include "format.h"
#include "fat_label.h"
#include "verbose_printf.h"



static void printHelp(void)
{
	puts("sdFormatLinux 0.1.0 by profi200\n"
	     "Usage: sdFormatLinux [OPTIONS...] DEVICE\n\n"
	     "Options:\n"
	     "  -l, --label LABEL        Volume label. Maximum 11 uppercase characters.\n"
	     "  -e, --erase TYPE         Erases the whole card before formatting (TRIM).\n"
	     "                           No effect with USB card readers.\n"
	     "                           TYPE should be 'trim'.\n"
	     "  -f, --force-fat32        Force FAT32 for SDXC cards.\n"
	     "  -c, --capacity SECTORS   Override capacity for fake cards.\n"
	     "  -b, --big-clusters       NOT RECOMMENDED. In combination with -f on SDXC cards\n"
	     "                           this will set the logical sector size higher than 512\n"
	     "                           to bypass the FAT32 64 KiB cluster size limit.\n"
	     "                           Many FAT drivers including the one in Windows\n"
	     "                           will not mount the filesystem or corrupt it!\n"
	     "  -v, --verbose            Show format details.\n"
	     "  -h, --help               Output this help.\n");
}

int main(const int argc, char *const argv[])
{
	setlocale(LC_CTYPE, ""); // We could also default to "en_US.UTF-8".

	static const struct option long_options[] =
	{{"big-clusters",       no_argument, NULL, 'b'},
	 {    "capacity", required_argument, NULL, 'c'},
	 {       "erase", required_argument, NULL, 'e'},
	 { "force-fat32",       no_argument, NULL, 'f'},
	 {       "label", required_argument, NULL, 'l'},
	 {     "verbose",       no_argument, NULL, 'v'},
	 {        "help",       no_argument, NULL, 'h'},
	 {          NULL,                 0, NULL,   0}};

	u64 overrTotSec = 0;
	ArgFlags flags{};
	char label[12]{};
	while(1)
	{
		const int c = getopt_long(argc, argv, "bc:e:fl:vh", long_options, NULL);
		if(c == -1) break;

		switch(c)
		{
			case 'b':
				flags.bigClusters = 1;
				break;
			case 'c':
				{
					// Temporary limit of 2 TiB.
					overrTotSec = strtoull(optarg, NULL, 0);
					if(overrTotSec == 0 || overrTotSec > 1ull<<32)
					{
						fputs("Error: Capacity 0 or out of range.\n", stderr);
						return ERR_INVALID_ARG;
					}
				}
				break;
			case 'e':
				{
					// TODO: Support full overwrite?
					if(strcmp(optarg, "trim") == 0)
						flags.erase = 1;
					else if(strcmp(optarg, "secure") == 0)
						flags.secErase = 1;
					else
					{
						fprintf(stderr, "Error: Invalid erase type '%s'.\n", optarg);
						return ERR_INVALID_ARG;
					}
				}
				break;
			case 'f':
				flags.forceFat32 = 1;
				break;
			case 'l':
				{
					if(convertCheckLabel(optarg, label) == 0)
						return ERR_INVALID_ARG;
				}
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
				return ERR_INVALID_ARG;
		}
	}

	if(argc - optind < 1 || argc - optind > 1)
	{
		printHelp();
		return ERR_INVALID_ARG;
	}

	const char *const devPath = argv[optind];
	int res;
	try
	{
		setVerboseMode(flags.verbose);
		res = formatSd(devPath, label, flags, overrTotSec);
	}
	catch(const std::exception &e)
	{
		fprintf(stderr, "An exception occurred: what(): '%s'\n", e.what());
		res = ERR_EXCEPTION;
	}
	catch(...)
	{
		fprintf(stderr, "Unknown exception. Aborting...\n");
		res = ERR_UNK_EXCEPTION;
	}

	return res;
}

# sdFormatLinux
Unfortunately SDFormatter [was](https://www.sdcard.org/downloads/sd-memory-card-formatter-for-linux) not available for Linux so i made this. It is a tool to format your SD card the way the SD Association intended but under Linux. It offers a few features the official tool does not like FAT32 for SDXC cards.

Multiple SD cards with different capacities have been tested and this tool formats them 1:1 the same as SDFormatter with the following exceptions:
* SDFormatter does not set the jmp instruction offset in the boot sector for FAT12/16/32. sdFormatLinux does.
* For exFAT sdFormatLinux clears unused FAT entries SDFormatter leaves untouched and SDFormatter clears more areas after root directory cluster. As far as i can tell these differences don't matter.
* sdFormatLinux currently does not preserve OEM flash parameters when reformatting in exFAT. It will recalculate the correct values instead.

## Examples
Erase (TRIM) and format SD card (recommended). TRIM will not work with USB card readers and is ignored if used with one.  
`sudo sdFormatLinux -e trim /dev/mmcblkX` where X is a number.

Erase and format with label.  
`sudo sdFormatLinux -l 'MY LABEL' -e trim /dev/mmcblkX`

## Compiling
Just run `make`. It automatically builds a hardened version.

## License
This software is licensed under the MIT license. See LICENSE.txt for details.

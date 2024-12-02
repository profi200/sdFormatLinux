# sdFormatLinux
Unfortunately SDFormatter [was](https://www.sdcard.org/downloads/sd-memory-card-formatter-for-linux) not available for Linux so i made this. It is a tool to format your SD card the way the SD Association intended but under Linux. It offers a few features the official tool does not like FAT32 for SDXC cards.

Multiple SD cards with different capacities have been tested and this tool formats them 1:1 the same as SDFormatter with the following exceptions:
* SDFormatter does not set the jmp instruction offset in the boot sector for FAT12/16/32. sdFormatLinux does.
* For exFAT sdFormatLinux clears the area between last FAT entry and cluster heap but SDFormatter doesn't. As far as i can tell this difference doesn't matter.
* sdFormatLinux currently does not preserve OEM flash parameters when reformatting in exFAT. It will recalculate the correct values instead.

## Examples
Erase (TRIM) and format SD card (recommended). TRIM will not work with USB card readers and is ignored if used with one.  
`sudo sdFormatLinux -e trim /dev/mmcblkX` where X is a number.

Erase and format with label.  
`sudo sdFormatLinux -l 'MY LABEL' -e trim /dev/mmcblkX`

Erase and format a SDXC card to FAT32 (64 KiB clusters).  
`sudo sdFormatLinux -e trim -f /dev/mmcblkX`

## FAQ
**Q: Why should i format my SDXC card with this tool to FAT32 instead of using guiformat/other tools?**\
A: Because most of these tools are not designed for flash based media and will format them incorrectly causing lower lifespan and performance.  
There is a common myth that you should only use 32 KB (actually KiB) clusters which is false. sdFormatLinux will use 64 KiB clusters when formatting SDXC cards to FAT32 and it works in every device compliant to Microsoft's FAT specification.

## Benchmark results
guiformat with 32 KiB clusters:  
![guiformat 32 KiB clusters](https://github.com/profi200/sdFormatLinux/blob/master/res/guiformat_32KiB.png?raw=true "guiformat 32 KiB clusters")

guiformat with 64 KiB clusters:  
![guiformat 64 KiB clusters](https://github.com/profi200/sdFormatLinux/blob/master/res/guiformat_64KiB.png?raw=true "guiformat 64 KiB clusters")

sdFormatLinux with FAT32 and 64 KiB clusters:  
![sdFormatLinux FAT32 64 KiB clusters](https://github.com/profi200/sdFormatLinux/blob/master/res/sdFormatLinux_force_FAT32_64KiB.png?raw=true "sdFormatLinux FAT32 64 KiB clusters")

SDFormatter with exFAT and 128 KiB clusters:  
![SDFormatter exFAT 128 KiB clusters](https://github.com/profi200/sdFormatLinux/blob/master/res/SDFormatter_exFAT_128KiB.png?raw=true "SDFormatter exFAT 128 KiB clusters")

## Compiling
Just run `make`. It automatically builds a hardened version.

## License
This software is licensed under the MIT license. See LICENSE.txt for details.

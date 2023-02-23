# sdFormatLinux
Unfortunately SDFormatter isn't available for Linux so i made this. It is a tool to format your SD card the way the SD Association intended but under Linux.

Multiple SD cards with different capacities have been tested and this tool formats them 1:1 the same as SDFormatter with the following exceptions:
* SDFormatter sets end head in the MBR to 254 for a card with 31074304 sectors (16 GB) which is a bug i believe. sdFormatLinux uses 255.
* sdFormatLinux does not support exFAT right now.

## Examples
Erase (TRIM) and format SD card (recommended). TRIM will not work with USB card readers and is ignored.  
`sudo sdFormatLinux -e trim /dev/mmcblkX` where X is a number.

Erase and format with label.  
`sudo sdFormatLinux -l 'MY LABEL' -e trim /dev/mmcblkX`

## Compiling
Just run `make`. It automatically builds a hardened version.

## License
This software is licensed under the MIT license. See LICENSE.txt for details.
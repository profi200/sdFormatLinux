#!/bin/bash
rm ./sdFormatLinux
gcc -std=c17 -s -flto -O2 -fstrict-aliasing -ffunction-sections -fdata-sections -Wall -Wextra -Wl,--gc-sections ./sdFormatLinux.c ./fs_printer.c ./format.c ./blockdev.c -lm -o ./sdFormatLinux
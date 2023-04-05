// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include "types.h"


// http://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/PC/CP850.TXT
// Range 0x80-0xFF.
static const wchar_t cp850Lut[128] =
{
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
	0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
	0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
	0x00FF, 0x00D6, 0x00DC, 0x00F8, 0x00A3, 0x00D8, 0x00D7, 0x0192,
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
	0x00BF, 0x00AE, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x00C1, 0x00C2, 0x00C0,
	0x00A9, 0x2563, 0x2551, 0x2557, 0x255D, 0x00A2, 0x00A5, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x00E3, 0x00C3,
	0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x00A4,
	0x00F0, 0x00D0, 0x00CA, 0x00CB, 0x00C8, 0x0131, 0x00CD, 0x00CE,
	0x00CF, 0x2518, 0x250C, 0x2588, 0x2584, 0x00A6, 0x00CC, 0x2580,
	0x00D3, 0x00DF, 0x00D4, 0x00D2, 0x00F5, 0x00D5, 0x00B5, 0x00FE,
	0x00DE, 0x00DA, 0x00DB, 0x00D9, 0x00FD, 0x00DD, 0x00AF, 0xACCE,
	0x00AD, 0x00B1, 0x2017, 0x00BE, 0x00B6, 0x00A7, 0x00F7, 0x00B8,
	0x00B0, 0x00A8, 0x00B7, 0x00B9, 0x00B3, 0x00B2, 0x25A0, 0x00A0
};



// Make sure dosStr is big enough!
static bool wchar2cp850(const wchar_t *wStr, char *dosStr)
{
	while(*wStr != L'\0')
	{
		const wchar_t wc = *wStr++;
		if(wc > 0 && wc < 128)
		{
			*dosStr++ = wc;
			continue;
		}

		unsigned i;
		for(i = 0; i < 128; i++)
		{
			if(wc == cp850Lut[i])
			{
				*dosStr++ = 0x80 | i;
				break;
			}
		}

		if(i == 128)
		{
			fprintf(stderr, "Error: Can't convert wide character 0x%08" PRIX32 " to CP850.\n", wc);
			return false;
		}
	}

	*dosStr = '\0';

	return true;
}

// Caution! This expects wcs can hold 13 chars including termination.
// This way we will know if the string contains more than 11 codepoints.
static size_t label2wchar(const char *const label, wchar_t *const wcs)
{
	wcs[12] = L'\0';
	const size_t wLength = mbstowcs(wcs, label, 12);
	if(wLength == (size_t)-1)
	{
		fputs("Failed to convert label to wide characters.\n", stderr);
		return 0;
	}

	if(wLength > 11)
	{
		fputs("Error: Label is too long.\n", stderr);
		return 0;
	}

	return wLength;
}

size_t convertCheckFatLabel(const char *const label, char *dosLabel)
{
	wchar_t wLabel[13];
	const size_t wLength = label2wchar(label, wLabel);
	if(wLength == 0) return 0;

	for(unsigned i = 0; wLabel[i] != L'\0'; i++)
	{
		if(iswlower(wLabel[i]) != 0)
		{
			fputs("Warning: Label contains lowercase characters.\n", stderr);
			break;
		}
	}

	if(!wchar2cp850(wLabel, dosLabel)) return 0;

	if(*dosLabel == ' ')
	{
		fputs("Error: First character in label must nut be a space.\n", stderr);
		return 0;
	}

	while(*dosLabel != '\0')
	{
		const unsigned char c = *dosLabel++;
		if(c < 0x20 || c == 0x22 ||
		   (c >= 0x2A && c <= 0x2C) ||
		   c == 0x2E || c == 0x2F ||
		   (c >= 0x3A && c <= 0x3F) ||
		   (c >= 0x5B && c <= 0x5D) ||
		   c == 0x7C)
		{
			fputs("Error: Label contains invalid characters.\n", stderr);
			return 0;
		}
	}

	return wLength;
}

size_t convertCheckExfatLabel(const char *const label, char16_t *utf16Label)
{
	wchar_t wLabel[13];
	if(label2wchar(label, wLabel) == 0) return 0;

	size_t length = 0;
	wchar_t *wPtr = wLabel;
	while(length < 11 && *wPtr != L'\0')
	{
		const wchar_t wc = *wPtr++;
		length++;
		if(wc < 0x10000)
		{
			*utf16Label++ = wc;
			continue;
		}

		if(length++ == 11)
		{
			fputs("Error: Label is too long.\n", stderr);
			return 0;
		}

		// Encode as surrogate pair.
		const wchar_t tmp = wc - 0x10000;
		*utf16Label++ = 0xD800u + (tmp>>10);
		*utf16Label++ = 0xDC00u + (tmp & 0x3FFu);
	}

	*utf16Label = u'\0';

	return length;
}

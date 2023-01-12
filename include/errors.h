#pragma once


enum
{
	// Everything ok = 0.
	ERR_INVALID_ARG   = 1,
	ERR_DEV_OPEN      = 2,
	ERR_DEV_TOO_SMALL = 3,
	ERR_ERASE         = 4,
	ERR_PARTITION     = 5,
	ERR_FORMAT        = 6,
	ERR_CLOSE_DEV     = 7,
	ERR_EXCEPTION     = 8,
	ERR_UNK_EXCEPTION = 9
};

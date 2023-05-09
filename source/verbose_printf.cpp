// SPDX-License-Identifier: MIT
// Copyright (c) 2023 profi200

#include <cstdarg>
#include <cstdio>


static bool g_verbose = false;



void setVerboseMode(const bool verbose)
{
	g_verbose = verbose;
}

int verbosePuts(const char *str)
{
	int res = 0;
	if(g_verbose == true) res = puts(str);
	return res;
}

int verbosePrintf(const char *format, ...)
{
	int res = 0;

	va_list args;
	va_start(args, format);

	if(g_verbose) res = vprintf(format, args);

	va_end(args);

	return res;
}

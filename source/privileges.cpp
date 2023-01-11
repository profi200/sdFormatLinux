#include <cstdlib>
#include <unistd.h>
#include "verbose_printf.h"



void dropPrivileges(void)
{
	const int uid = getuid();
	if(uid == -1) abort();
	else if(uid != 0) // Drop privilges when running as set-user-ID program.
	{
		verbosePuts("Dropping privileges...");
		if(setgid(getgid()) == -1) abort();
		if(setuid(uid) == -1) abort();

		// Extra paranoid check.
		if(setuid(0) == 0) abort();
	}
}

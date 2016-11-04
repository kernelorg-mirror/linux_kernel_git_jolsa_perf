#include <stdio.h>
#include <stdarg.h>
#include "debug.h"

int verbose;

int veprintf(int level, int var, const char *fmt, va_list args)
{
	int ret = 0;

	if (var >= level)
		ret = vfprintf(stderr, fmt, args);

	return ret;
}

int eprintf(int level, int var, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = veprintf(level, var, fmt, args);
	va_end(args);

	return ret;
}

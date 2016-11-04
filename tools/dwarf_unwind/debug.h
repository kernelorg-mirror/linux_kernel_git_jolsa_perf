#ifndef __DEBUG_H
#define __DEBUG_H

extern int verbose;

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define pr_info(fmt, ...) \
	eprintf(0, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...) \
	eprintf(0, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_verbose(fmt, ...) \
	eprintf(1, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug(fmt, ...) \
	eprintf(2, verbose, pr_fmt(fmt), ##__VA_ARGS__)

int eprintf(int level, int var, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#endif /* __DEBUG_H */

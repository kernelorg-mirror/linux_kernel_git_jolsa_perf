#include <stdlib.h>
#include <stdio.h>

char *target;
char *depfile;
char *cmdline;

static void usage(void)
{
	fprintf(stderr, "Usage: fixdep <depfile> <target> <cmdline>\n");
	exit(1);
}

/*
 * Print out the commandline prefixed with cmd_<target filename> :=
 */
static void print_cmdline(void)
{
	printf("cmd_%s := %s\n\n", target, cmdline);
}

int main(int argc, char **argv)
{
	if (argc != 4)
		usage();

	depfile = argv[1];
	target  = argv[2];
	cmdline = argv[3];

	print_cmdline();
	return 0;
}

#include <stdio.h>

int main(int argc, char **argv)
{
	fprintf(stdout, ".pushsection __unwind_data,\"a\"\n");
	fprintf(stdout, ".byte 0x0\n");
	fprintf(stdout, ".byte 0x1\n");
	fprintf(stdout, ".byte 0x2\n");
	fprintf(stdout, ".byte 0x3\n");
	fprintf(stdout, ".popsection\n");
	return 0;
}

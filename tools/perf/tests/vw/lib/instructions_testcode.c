#include "test_utils.h"
#include "instructions_testcode.h"

/*
 * Test a simple loop of 1 million instructions.
 * Most implementations should count be correct within 1%.
 * This loop in in assembly language, as compiler generated
 * code varies too much.
 */

int instructions_million(void)
{
#if defined(__i386__) || (defined __x86_64__)
	asm("\txor %%ecx,%%ecx\n"
	    "\tmov $499999,%%ecx\n"
	    "test_loop:\n"
	    "\tdec %%ecx\n"
	    "\tjnz test_loop\n"
	    : /* no output registers */
	    : /* no inputs */
	    : "cc", "%ecx" /* clobbered */
	);
	return 0;
#elif defined(__PPC__)
	asm("\tnop                           # to give us an even million\n"
	    "\tlis     15,499997@ha          # load high 16-bits of counter\n"
	    "\taddi    15,15,499997@l        # load low 16-bits of counter\n"
	    "55:\n"
	    "\taddic.  15,15,-1              # decrement counter\n"
	    "\tbne     0,55b                  # loop until zero\n"
	    : /* no output registers */
	    : /* no inputs */
	    : "cc", "15" /* clobbered */
	);
	return 0;
#elif defined(__ia64__)
	asm("\tmov     loc6=166666           // below is 6 instr.\n"
	    ";;                              // because of that we count 4 too few\n"
	    "55:\n"
	    "\tadd     loc6=-1,loc6          // decrement count\n"
	    ";;\n"
	    "\tcmp.ne  p2,p3=0,loc6\n"
	    "(p2)    br.cond.dptk    55b     // if not zero, loop\n"
	    : /* no output registers */
	    : /* no inputs */
	    : "p2", "loc6" /* clobbered */
	);
	return 0;
#elif defined(__sparc__)
	asm("\tsethi     %%hi(333333), %%l0\n"
	    "\tor        %%l0,%%lo(333333),%%l0\n"
	    "test_loop:\n"
	    "\tdeccc   %%l0             ! decrement count\n"
	    "\tbnz     test_loop        ! repeat until zero\n"
	    "\tnop                      ! branch delay slot\n"
	    : /* no output registers */
	    : /* no inputs */
	    : "cc", "l0" /* clobbered */
	);
	return 0;
#elif defined(__arm__)
	asm("\tldr     r2,count                    @ set count\n"
	    "\tb       test_loop\n"
	    "count:       .word 333332\n"
	    "test_loop:\n"
	    "\tadd     r2,r2,#-1\n"
	    "\tcmp     r2,#0\n"
	    "\tbne     test_loop                    @ repeat till zero\n"
	    : /* no output registers */
	    : /* no inputs */
	    : "cc", "r2" /* clobbered */
	);
    return 0;
#endif
    return CODE_UNIMPLEMENTED;
}

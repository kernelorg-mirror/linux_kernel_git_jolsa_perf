%option prefix="perf_rdt_"

%{
#include <stdlib.h>
#include "rdt-bison.h"
#include "rdt.h"

static int value(int base)
{
	long num;

	errno = 0;
	num = strtoul(perf_rdt_text, NULL, base);
	if (errno)
		return RDT_ERROR;

	perf_rdt_lval.num = num;
	return RDT_VALUE;
}

static int str(int token)
{
	perf_rdt_lval.str = strdup(perf_rdt_text);
	return token;
}

%}

%x schemata

num_dec		[0-9]+
num_hex		0x[0-9a-fA-F]+
map		[0-9,-]+
name		[a-zA-Z_*?][a-zA-Z0-9_*?.]*

%%

<schemata>{
[0-9a-f]+	{ return str(RDT_SVALUE);		}
{name}		{ return str(RDT_NAME);			}
"}"		{ BEGIN(INITIAL); return '}';		}
"{"		{ return '{';				}
=		{ return '=';				}
:		{ return ':';				}
;		{ return ';';				}
\n		{ ; }
\t		{ ; }
.		{ ; }
}

{num_dec}	{ return value(10);			}
{num_hex}	{ return value(16);			}
resource	{ return RDT_RESOURCE;			}
group		{ return RDT_GROUP;			}
cbm_mask	{ return RDT_CBM_MASK;			}
min_cbm_bits	{ return RDT_CBM_BITS;			}
num_closids	{ return RDT_CLOSIDS;			}
id		{ return RDT_ID;			}
ids		{ return RDT_IDS;			}
cpus		{ return RDT_CPUS;			}
tasks		{ return RDT_TASKS;			}
schemata	{ BEGIN(schemata); return RDT_SCHEMATA;	}
{map}		{ return str(RDT_MAP);			}
{name}		{ return str(RDT_NAME);			}
"{"		{ return '{';				}
"}"		{ return '}';				}
=		{ return '=';				}
\n		{ ; }
\t		{ ; }
.		{ ; }

%%

int perf_rdt_wrap(void)
{
	return 1;
}

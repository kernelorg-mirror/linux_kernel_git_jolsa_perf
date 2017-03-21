%parse-param {void *_data}

%{

#include <linux/compiler.h>
#include "rdt.h"

extern int perf_rdt_lex(void);

#define ABORT_ON(val) \
do { \
        if (val) \
                YYABORT; \
} while (0)

%}

%token RDT_RESOURCE
%token RDT_GROUP
%token RDT_CBM_MASK
%token RDT_CBM_BITS
%token RDT_CLOSIDS
%token RDT_IDS
%token RDT_CPUS
%token RDT_TASKS
%token RDT_SCHEMATA
%token RDT_MAP
%token RDT_NAME
%token RDT_VALUE
%token RDT_ERROR

%type <num> RDT_VALUE
%type <str> RDT_NAME

%union
{
	unsigned long num;
	char *str;
}

%%

start:
start resource
|
start group
|
resource
|
group

resource: RDT_RESOURCE RDT_NAME

group: RDT_GROUP RDT_NAME

%%

void perf_rdt_error(struct list_head *list __maybe_unused,
		    char *name __maybe_unused,
		    char const *msg __maybe_unused)
{
}

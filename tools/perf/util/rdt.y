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
%token RDT_SCHEMATA_ASS
%token RDT_MAP
%token RDT_NAME
%token RDT_VALUE
%token RDT_SVALUE
%token RDT_ERROR

%type <num> RDT_VALUE
%type <str> RDT_SVALUE
%type <str> RDT_NAME
%type <str> RDT_MAP
%type <str> RDT_CPUS

%union
{
	unsigned long num;
	char *str;
}

%%

start:
start resource
{
	printf("KRAVA start resource\n");
}
|
start group
{
	printf("KRAVA start group\n");
}
|
resource
{
	printf("KRAVA resource\n");
}
|
group
{
	printf("KRAVA group\n");
}

resource:
RDT_RESOURCE RDT_NAME '{' resource_def '}'
{
	printf("KRAVA resource NAME\n");
}

resource_def:
resource_def resource_config
{
}
|
resource_config
{
}

resource_config:
RDT_CBM_MASK '=' RDT_VALUE
{
}
|
RDT_CBM_BITS '=' RDT_VALUE
{
}
|
RDT_CLOSIDS '=' RDT_VALUE
{
}
|
RDT_IDS '=' '{' ids_def '}'
{
}

ids_def:
ids_def map
{
}
|
map
{
}

map:
RDT_VALUE '=' RDT_MAP
{
}

group:
RDT_GROUP RDT_NAME '{' group_def '}'
{
	printf("KRAVA group NAME\n");
}

group_def:
group_def group_config
{
}
|
group_config
{
}

group_config:
RDT_CPUS '=' RDT_MAP
{
}
|
RDT_SCHEMATA '=' '{' schemata_def '}'
{
}

schemata_def:
schemata_def schemata_line
{
}
|
schemata_line
{
}

schemata_line:
RDT_NAME ':' schemata_line_config

schemata_line_config:
schemata_line_config ';' schemata_ass
|
schemata_ass

schemata_ass:
RDT_SVALUE '=' RDT_SVALUE

%%

void perf_rdt_error(struct list_head *list __maybe_unused,
		    char *name __maybe_unused,
		    char const *msg __maybe_unused)
{
}

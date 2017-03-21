%parse-param {void *_data}

%{

#include <stdlib.h>
#include <linux/compiler.h>
#include "rdt.h"

extern int perf_rdt_lex(void);

static struct rdt_config* rdt_config__alloc(struct rdt_config *templ)
{
	struct rdt_config *c = malloc(sizeof(*templ));

	if (c) {
		*c = *templ;
		INIT_LIST_HEAD(&c->list);
	}

	return c;
}

#define __AP(__f) ({		\
	void *__v = __f;	\
	if (!__v) YYABORT;	\
	__v; })

#define __A(__f) ({		\
	int __v = __f;		\
	if (__v) YYABORT;	\
	__v; })

#define __C(__t, __c) ({			\
	(__c)->type = RDT_CONFIG_TYPE__ ## __t;	\
	__AP(rdt_config__alloc((__c))); })

#define __L(__h, __c) ({				\
	struct list_head *____h = (__h);		\
	if (!____h) {					\
		____h = __AP(malloc(sizeof(*____h)));	\
		INIT_LIST_HEAD(____h);			\
	}						\
	list_add_tail(&(__c)->list, ____h);		\
	____h; })

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
%type <config> schemata_ass
%type <config> schemata_line
%type <config> group_config
%type <config> resource_config
%type <config> id
%type <head> schemata_line_config
%type <head> schemata_def
%type <head> group_def
%type <head> ids_def
%type <head> resource_def

%union
{
	unsigned long num;
	char *str;
	struct rdt_config *config;
	struct list_head *head;
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

resource:
RDT_RESOURCE RDT_NAME '{' resource_def '}'
{
	__A(rdt_resource__add(_data, $2, $4));
}

resource_def:
resource_def resource_config
{
	$$ = __L($1, $2);
}
|
resource_config
{
	$$ = __L(NULL, $1);
}

resource_config:
RDT_CBM_MASK '=' RDT_VALUE
{
	struct rdt_config c = {
		.cbm_mask = $3,
	};

	$$ = __C(CBM_MASK, &c);
}
|
RDT_CBM_BITS '=' RDT_VALUE
{
	struct rdt_config c = {
		.min_cbm_bits = $3,
	};

	$$ = __C(MIN_CBM_BITS, &c);
}
|
RDT_CLOSIDS '=' RDT_VALUE
{
	struct rdt_config c = {
		.num_closids = $3,
	};

	$$ = __C(NUM_CLOSIDS, &c);
}
|
RDT_IDS '=' '{' ids_def '}'
{
	struct rdt_config c = {
		.ids.head = $4,
	};

	$$ = __C(IDS, &c);
}

ids_def:
ids_def id
{
	$$ = __L($1, $2);
}
|
id
{
	$$ = __L(NULL, $1);
}

id:
RDT_VALUE '=' RDT_MAP
{
	struct rdt_config c = {
		.id = {
			.val = $1,
			.map = $3,
		},
	};

	$$ = __C(ID, &c);
}

group:
RDT_GROUP RDT_NAME '{' group_def '}'
{
	__A(rdt_group__add(_data, $2, $4));
}

group_def:
group_def group_config
{
	$$ = __L($1, $2);
}
|
group_config
{
	$$ = __L(NULL, $1);
}

group_config:
RDT_CPUS '=' RDT_MAP
{
	struct rdt_config c = {
		.cpus.map = $3,
	};

	$$ = __C(CPUS, &c);
}
|
RDT_SCHEMATA '=' '{' schemata_def '}'
{
	struct rdt_config c = {
		.schemata.head = $4,
	};

	$$ = __C(SCHEMATA, &c);
}

schemata_def:
schemata_def schemata_line
{
	$$ = __L($1, $2);
}
|
schemata_line
{
	$$ = __L(NULL, $1);
}

schemata_line:
RDT_NAME ':' schemata_line_config
{
	struct rdt_config c = {
		.schemata_line = {
			.name = $1,
			.head = $3,
		},
	};

	$$ = __C(SCHEMATA_LINE, &c);
}

schemata_line_config:
schemata_line_config ';' schemata_ass
{
	$$ = __L($1, $3);
}
|
schemata_ass
{
	$$ = __L(NULL, $1);
}

schemata_ass:
RDT_SVALUE '=' RDT_SVALUE
{
	struct rdt_config c = {
		.schemata_ass = {
			.id  = $1,
			.val = $3,
		},
	};

	$$ = __C(SCHEMATA_ASS, &c);
}

%%

void perf_rdt_error(struct list_head *list __maybe_unused,
		    char *name __maybe_unused,
		    char const *msg __maybe_unused)
{
}

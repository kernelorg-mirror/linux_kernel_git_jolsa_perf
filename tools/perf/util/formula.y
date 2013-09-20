%pure-parser
%name-prefix "perf_formula_"
%parse-param {void *_data}
%parse-param {void *scanner}
%lex-param {void* scanner}

%left '+' '-' '*' '/'

%{

#define YYDEBUG 1

#include "util.h"
#include "formula.h"
#include "formula-bison.h"

extern int formula_lex(YYSTYPE* lvalp, void* scanner);


#define ABORT() YYABORT

#define ABORT_ON(val) \
do { \
	if (val) \
		YYABORT; \
} while (0)

#define HEAD() ({						\
	struct list_head *__head = zalloc(sizeof(*__head));	\
	ABORT_ON(!__head);					\
	INIT_LIST_HEAD(__head);					\
	__head;							\
})

#define CONFIG() ({						\
	struct perf_formula_config *__config;			\
	__config = zalloc(sizeof(*__config));			\
	ABORT_ON(!__config);					\
	INIT_LIST_HEAD(&__config->list);			\
	__config;						\
})

%}

%token PF_START_CONFIG PF_START_EXPR
%token PF_NAME
%token PF_VALUE
%token PF_FORMULA
%token PF_DESC
%token PF_EVENTS
%token PF_EOLN_STR
%token PF_ERROR
%token PF_PRINT

%type <str> PF_NAME
%type <num> PF_VALUE
%type <str> PF_EOLN_STR
%type <head> set_def
%type <config> set_token
%type <config> events_def
%type <config> ass
%type <result> expr

%union
{
	char *str;
	double num;
	struct list_head *head;
	struct config *config;
	struct perf_formula_counter *counter;
	struct perf_formula_saved_results *result;
}

%%

start:
PF_START_CONFIG start_config
|
PF_START_EXPR start_expr

start_config: sets

sets:
sets set | set

set:
PF_NAME '{' set_def '}'
{
	struct perf_formula_file *file = _data;
	struct perf_formula_set *set;

	set = perf_formula_set__new($1, $3);
	ABORT_ON(!set);

	list_add_tail(&set->list, &file->head_sets);
}

set_def:
set_def set_token
{
	struct list_head *head = $1;
	struct perf_formula_config *config = $2;

	list_add_tail(&config->list, head);
	$$ = head;
}
|
set_token
{
	struct list_head *head = HEAD();
	struct perf_formula_config *config = $1;

	list_add_tail(&config->list, head);
	$$ = head;
}

set_token:
PF_EVENTS '{' events_def '}'
{
	$$ = $3;
}
|
PF_PRINT PF_NAME
{
	struct perf_formula_config *config = CONFIG();

	config->type  = PERF_FORMULA_CONFIG_PRINT;
	config->print = strdup($2);

	$$ = config;
}
|
ass

/*
 * TODO add 'print ass' processing in here, like:
 * ...
 * |
 * PF_PRINT ass
 * {
 * ...
 * }
 */

events_def:
events_def ass
{
	struct perf_formula_config *config = $1;
	struct perf_formula_config *ass    = $2;

	list_add_tail(&ass->list, &config->events);

	$$ = config;
}
|
ass
{
	struct perf_formula_config *config = CONFIG();
	struct perf_formula_config *ass    = $1;

	config->type = PERF_FORMULA_CONFIG_EVENTS;
	INIT_LIST_HEAD(&config->events);
	list_add_tail(&ass->list, &config->events);

	$$ = config;
}

ass:
PF_NAME '=' PF_EOLN_STR
{
	struct perf_formula_config *config = CONFIG();

	config->type = PERF_FORMULA_CONFIG_ASS;
	config->ass.name  = strdup($1);
	config->ass.value = strdup($3);

	$$ = config;
}

start_expr:
expr
{
	struct perf_formula_expr *expr = _data;

	expr->result = $1;
}

expr:
PF_VALUE
{
	$$ = perf_formula__value(_data, $1);
}
|
PF_NAME
{
	$$ = perf_formula_expr__resolve(_data, $1);
}
|
'-' expr
{
	$$ = perf_formula__negate(_data, $2);
}
|
expr '+' expr
{
	$$ = perf_formula__add(_data, $1, $3);
}
|
expr '-' expr
{
	$$ = perf_formula__subtract(_data, $1, $3);
}
|
expr '*' expr
{
	$$ = perf_formula__multiple(_data, $1, $3);
}
|
expr '/' expr
{
	$$ = perf_formula__divide(_data, $1, $3);
}
|
'(' expr ')'
{
	$$ = $2;
}

%%

void perf_formula_error(void *data __maybe_unused,
			void *scanner __maybe_unused,
			char const *msg __maybe_unused)
{
}

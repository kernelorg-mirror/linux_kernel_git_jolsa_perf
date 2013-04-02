
%option reentrant
%option bison-bridge
%option prefix="perf_formula_"
%option stack

%{
#include "formula-bison.h"
#include "formula.h"

char *perf_formula_get_text(yyscan_t yyscanner);
YYSTYPE *perf_formula_get_lval(yyscan_t yyscanner);

static int __value(YYSTYPE *yylval, char *str, int base, int token)
{
	double num;

	errno = 0;
	num = strtoull(str, NULL, base);
	if (errno)
		return PF_ERROR;

	yylval->num = num;
	return token;
}

static int value(yyscan_t scanner, int base)
{
	YYSTYPE *yylval = perf_formula_get_lval(scanner);
	char *text = perf_formula_get_text(scanner);

	return __value(yylval, text, base, PF_VALUE);
}

static int str(yyscan_t scanner, int token)
{
	YYSTYPE *yylval = perf_formula_get_lval(scanner);
	char *text = perf_formula_get_text(scanner);

	yylval->str = strdup(text);
	return token;
}

%}

num_dec		[0-9]+
num_hex		0x[a-fA-F0-9]+
name		[a-zA-Z_*?][a-zA-Z0-9_*?\.-]*

%x config
%x config_eoln_str

%%

%{
	{
		int start_token;

		start_token = perf_formula_get_extra(yyscanner);

		if (start_token == PF_START_CONFIG)
			BEGIN(config);

		if (start_token) {
			perf_formula_set_extra(NULL, yyscanner);
			return start_token;
		}
	}
%}

<config>{
"{"		{ return '{'; }
"}"		{ return '}'; }
"="		{ BEGIN(config_eoln_str); return '='; }

events		{ return PF_EVENTS; }
print		{ return PF_PRINT; }
{name}		{ return str(yyscanner, PF_NAME); }

\n		{ }
.		{ }
}

<config_eoln_str>{
[^\n]+		{
			str(yyscanner, PF_EOLN_STR);
			BEGIN(config);
			return PF_EOLN_STR;
                }

<<EOF>>		{
			BEGIN(config);
		}
}

%%

int perf_formula_wrap(void *scanner __maybe_unused)
{
	return 1;
}

#ifndef __SCRIPT_SAMPLE_H
#define __SCRIPT_SAMPLE_H

#include "script-sample-api.h"
#include "dso.h"
#include "symbol.h"

struct script_symbol;

struct script_line {
	int			 lineno;
	struct script_symbol	*sym;
};

struct script_symbol {
	u64		 id;
	int		 line;
	char		*file;
	unsigned char	*lnotab;
};

static inline struct script_symbol *symbol__script_symbol(struct symbol *sym)
{
	return ((void *) sym) - symbol_conf.script_off;
}

int python_stack__read(int fd, struct python_header *header,
		       struct dso *dso, struct symbol **last_sym);

#endif /* __SCRIPT_SAMPLE_H */

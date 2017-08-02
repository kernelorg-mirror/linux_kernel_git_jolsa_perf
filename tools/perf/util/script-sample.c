#include <linux/compiler.h>
#include "script-sample.h"
#include "script-sample-api.h"
#include "util.h"
#include "debug.h"

static int python_func__read(int fd, struct python_header *header,
		      struct dso *dso, struct symbol **psym)
{
	struct python_func *func;
	char buf[PATH_MAX];
	struct script_symbol *ssym;
	struct symbol *sym;
	ssize_t len;

	func = (struct python_func *) buf;
	len = header->size - sizeof(*header);

	if (readn(fd, (void *) &func->start, len) != len)
		return -1;

	*psym = sym = symbol__new(func->start, func->end - func->start,
				  STB_GLOBAL, func->name);
	if (!sym)
		return -1;

	ssym = symbol__script_symbol(sym);
	ssym->id = header->id;

	symbols__insert(&dso->symbols[MAP__VARIABLE], sym);
	return 0;
}

static int python_file__read(int fd, struct python_header *header,
		      struct symbol *last_sym)
{
	struct script_symbol *ssym = symbol__script_symbol(last_sym);
	struct python_file *file;
	char buf[PATH_MAX];
	ssize_t len;

	if (!last_sym || header->id != ssym->id || ssym->file)
		return -1;

	file = (struct python_file *) buf;
	len = header->size - sizeof(*header);

	if (readn(fd, (void *) &file->name, len) != len)
		return -1;

	ssym->file = strdup(file->name);
	return ssym->file ? 0 : -1;
}

static int python_line__read(int fd, struct python_header *header,
		      struct symbol *last_sym)
{
	struct script_symbol *ssym = symbol__script_symbol(last_sym);
	unsigned char *buf;
	u64 size, tmp, tmp_size;

	if (!last_sym || header->id != ssym->id || ssym->lnotab)
		return -1;

	if (readn(fd, (void *) &size, sizeof(u64)) != sizeof(u64))
		return -1;

	buf = malloc(size);
	if (!buf)
		return -1;

	if (readn(fd, (void *) buf, size) != (ssize_t) size)
		return -1;

	tmp_size = header->size - sizeof(struct python_line) - size;

	if (readn(fd, (void *) &tmp, tmp_size) != (ssize_t) tmp_size)
		return -1;

	ssym->lnotab = buf;
	return 0;
}

int python_stack__read(int fd, struct python_header *header,
		       struct dso *dso, struct symbol **last_sym)
{
	int err;

	pr_debug("python stack event: type %u, id %lu\n",
		 header->type, header->id);

	switch (header->type) {
	case PYTHON_DUMP__FUNC:
		err = python_func__read(fd, header, dso, last_sym);
		break;
	case PYTHON_DUMP__FILE:
		err = python_file__read(fd, header, *last_sym);
		break;
	case PYTHON_DUMP__LINE:
		err = python_line__read(fd, header, *last_sym);
		break;
	default:
		pr_err("failed: unknown event %u\n", header->type);
		err = -1;
		break;
	};

	return err;
}

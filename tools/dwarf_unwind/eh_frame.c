#include <libelf.h>
#include <gelf.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "eh_frame.h"
#include "debug.h"

static Elf_Scn *elf_section_by_name(Elf *elf, GElf_Ehdr *ep,
				    GElf_Shdr *shp, const char *name, size_t *idx)
{
	Elf_Scn *sec = NULL;
	size_t cnt = 1;

	/* Elf is corrupted/truncated, avoid calling elf_strptr. */
	if (!elf_rawdata(elf_getscn(elf, ep->e_shstrndx), NULL))
		return NULL;

	while ((sec = elf_nextscn(elf, sec)) != NULL) {
		char *str;

		gelf_getshdr(sec, shp);
		str = elf_strptr(elf, ep->e_shstrndx, shp->sh_name);
		if (str && !strcmp(name, str)) {
			if (idx)
				*idx = cnt;
			return sec;
		}
		++cnt;
	}

	return NULL;
}

int eh_frame(Elf **_elf, int fd, unsigned long *start, unsigned long *stop)
{
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	Elf_Data *data;
	Elf_Scn *sec;
	Elf_Kind ek;
	Elf *elf;
	int err = -1;

	elf_version(EV_CURRENT);

	elf = elf_begin(fd, ELF_C_READ_MMAP, NULL);
	if (elf == NULL) {
		pr_err("%s: cannot read ELF file - %s.\n", __func__, elf_errmsg(elf_errno()));
		goto out_close;
	}

	ek = elf_kind(elf);
	if (ek != ELF_K_ELF)
		goto out_elf_end;

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		pr_err("%s: cannot get elf header.\n", __func__);
		goto out_elf_end;
	}

	sec = elf_section_by_name(elf, &ehdr, &shdr, ".eh_frame", NULL);

	data = elf_getdata(sec, NULL);
	if (data == NULL)
		goto out_elf_end;

	*_elf  = elf;
	*start = (unsigned long) data->d_buf;
	*stop  = (unsigned long) data->d_buf + data->d_size;

	eh_frame_base = shdr.sh_addr;
	eh_frame_ptr  = (unsigned long) *start;

	pr_verbose("eh_frame_base 0x%lx\n", eh_frame_base);
	pr_verbose("eh_frame_ptr  0x%lx\n", eh_frame_ptr);

	return 0;

out_elf_end:
	elf_end(elf);
out_close:
	close(fd);
	return err;;
}

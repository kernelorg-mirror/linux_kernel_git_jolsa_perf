#include <stdio.h>
#include <libelf.h>
#include <gelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

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

static int get_ehframe(Elf **_elf, int fd, unsigned long *start, unsigned long *stop)
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
		fprintf(stderr, "%s: cannot read ELF file - %s.\n", __func__, elf_errmsg(elf_errno()));
		goto out_close;
	}

	ek = elf_kind(elf);
	if (ek != ELF_K_ELF)
		goto out_elf_end;

	if (gelf_getehdr(elf, &ehdr) == NULL) {
		fprintf(stderr, "%s: cannot get elf header.\n", __func__);
		goto out_elf_end;
	}

	sec = elf_section_by_name(elf, &ehdr, &shdr, ".eh_frame", NULL);

	data = elf_getdata(sec, NULL);
	if (data == NULL)
		goto out_elf_end;

	*_elf  = elf;
	*start = (unsigned long) data->d_buf;
	*stop  = (unsigned long) data->d_buf + data->d_size;
	return 0;

out_elf_end:
	elf_end(elf);
out_close:
	close(fd);
	return err;;
}

static int parse_ehframe(unsigned long start, unsigned long stop)
{
	return 0;
}

int main(int argc, char **argv)
{
	Elf *elf;
	int fd;
	unsigned long start, stop;
	char *obj = argv[1];

	if (argc != 2)
		return -1;

	fd = open(obj, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "faield to open %s\n", obj);
		return -1;
	}

	if (get_ehframe(&elf, fd, &start, &stop))
		return -1;

	if (parse_ehframe(start, stop))
		return -1;

	fprintf(stdout, ".pushsection __unwind_data,\"a\"\n");
	fprintf(stdout, ".byte 0x0\n");
	fprintf(stdout, ".byte 0x1\n");
	fprintf(stdout, ".byte 0x2\n");
	fprintf(stdout, ".byte 0x3\n");
	fprintf(stdout, ".popsection\n");

	elf_end(elf);
	close(fd);
	return 0;
}

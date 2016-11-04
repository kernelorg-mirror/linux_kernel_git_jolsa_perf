#include <stdio.h>
#include <libelf.h>
#include <gelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/bpf.h>
#include "parse.h"

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

	eh_frame_base = shdr.sh_addr;
	eh_frame_ptr  = (unsigned long) *start;

	fprintf(stderr, "eh_frame_base 0x%lx\n", eh_frame_base);
	fprintf(stderr, "eh_frame_ptr  0x%lx\n", eh_frame_ptr);

	return 0;

out_elf_end:
	elf_end(elf);
out_close:
	close(fd);
	return err;;
}

#define BPF_LD_IMM641(DST, IMM)					\
	BPF_LD_IMM64_RAW1(DST, 0, IMM)

#define BPF_LD_IMM642(DST, IMM)					\
	BPF_LD_IMM64_RAW2(DST, 0, IMM)

#define BPF_LD_IMM64_RAW1(DST, SRC, IMM)			\
	((struct bpf_insn) {					\
		.code  = BPF_LD | BPF_DW | BPF_IMM,		\
		.dst_reg = DST,					\
		.src_reg = SRC,					\
		.off   = 0,					\
		.imm   = (__u32) (IMM) })

#define BPF_LD_IMM64_RAW2(DST, SRC, IMM)			\
	((struct bpf_insn) {					\
		.code  = 0, /* zero is reserved opcode */	\
		.dst_reg = 0,					\
		.src_reg = 0,					\
		.off   = 0,					\
		.imm   = ((__u64) (IMM)) >> 32 })

#define BPF_EMIT_CALL_UNWIND()					\
	((struct bpf_insn) {					\
		.code  = BPF_JMP | BPF_CALL,			\
		.dst_reg = 0,					\
		.src_reg = 0,					\
		.off   = 0,					\
		.imm   = BPF_FUNC_unwind })

#define BPF_EXIT_INSN()						\
	((struct bpf_insn) {					\
		.code  = BPF_JMP | BPF_EXIT,			\
		.dst_reg = 0,					\
		.src_reg = 0,					\
		.off   = 0,					\
		.imm   = 0 })

static void emit_frame(struct du_fde *fde, struct bpf_insn *insn, int len)
{
	static int idx;
	int i;

	fprintf(stdout, "struct bpf_insn ");
	fprintf(stdout, "__attribute__((section(\"__unwind_data\"))) ");
	fprintf(stdout, "insn_%d[%d] = {\n", idx, len);

	for (i = 0; i < len; i++) {
		struct bpf_insn *bi = &insn[i];

		fprintf(stdout, "	{ 0x%x, 0x%x, 0x%x, 0x%x, 0x%x },\n",
			bi->code, bi->dst_reg, bi->src_reg, bi->off, bi->imm);
	}

	fprintf(stdout, "};\n");

	fprintf(stdout, "\n");

	fprintf(stdout, "struct unwind_frame ");
	fprintf(stdout, "__attribute__((section(\"__unwind_data\"))) ");
	fprintf(stdout, "frame_%d = {\n", idx);
	fprintf(stdout, "	.loc_start = (__u8 *) 0x%lx,\n", fde->loc_start);
	fprintf(stdout, "	.loc_end   = (__u8 *) 0x%lx,\n", fde->loc_end);
	fprintf(stdout, "	.len       = %d,\n", len);
	fprintf(stdout, "	.insn      = insn_%d,\n", idx);
	fprintf(stdout, "};\n");

	fprintf(stdout, "\n");

	fprintf(stdout, "struct unwind_frame* ");
	fprintf(stdout, "__attribute__((section(\"__unwind_frame\"))) ");
	fprintf(stdout, "frame_ptr_%d = &frame_%d;\n", idx, idx);

	fprintf(stdout, "\n");

	idx++;
}


int fde_cb(struct du_fde *fde)
{
	struct bpf_insn code[4];
	unsigned long addr = (unsigned long) fde->loc_start;

	memset(code, 0x0, sizeof(code[0]) * 4);

	code[0] = BPF_LD_IMM641(BPF_REG_1, addr);
	code[1] = BPF_LD_IMM642(BPF_REG_1, addr);
	code[2] = BPF_EMIT_CALL_UNWIND();
	code[3] = BPF_EXIT_INSN();

	emit_frame(fde, code, 4);
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

	fprintf(stdout, "#include <linux/unwind.h>\n");
	fprintf(stdout, "\n");

	if (parse_limits((u8 *) start, (u8 *) stop))
		return -1;

	if (walk_fdes(fde_cb))
		return -1;

	elf_end(elf);
	close(fd);
	return 0;
}

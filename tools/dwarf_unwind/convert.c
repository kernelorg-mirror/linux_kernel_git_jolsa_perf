#include <stdio.h>
#include <stdlib.h>
#include <libelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/bpf.h>
#include <linux/dwarf_unwind.h>
#include <subcmd/parse-options.h>
#include <asm/errno.h>
#include <asm/bug.h>
#include "parse.h"
#include "read.h"
#include "debug.h"
#include "eh_frame.h"
#include "bpf.h"
#include "code.h"
#include "convert.h"

#define MAX_EXPR 10

static struct unw_code code_buf;
static struct unw_code _expr_buf[MAX_EXPR];
static struct unw_code *expr_buf = (struct unw_code *) &_expr_buf;

static bool comments;
static bool debug;

static int write_insn(struct unw_insn *insn, int len)
{
	int ret, i;

	for (i = 0; i < len; i++) {
		struct bpf_insn *bi = &insn[i].bi;

		ret = fprintf(stdout, "	{ 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x },",
			      bi->code, bi->dst_reg, bi->src_reg, bi->off, bi->imm);

		if (comments) {
			char *s = insn[i].cstr ? insn[i].cstr : insn[i].astr;

			if (s) {
				int indent = 50 - ret;
				ret += fprintf(stdout, "%*s/* %s ", indent, " ", s);

				indent = 100 - ret;
				ret = fprintf(stdout, "%*s */", indent, " ");
			}
		}
		fprintf(stdout, "\n");
	}

	return 0;
}

static int write_frame(struct unw_convert *c)
{
	struct unw_code *code = c->code;
	struct unw_fde *fde = c->fde;
	static int idx;
	int i, ret;

	fprintf(stdout, "struct bpf_insn ");
	fprintf(stdout, "__attribute__((section(\"__unwind_data\"))) ");
	fprintf(stdout, "insn_%d[%d] = {\n", idx, code->len);

	write_insn(code->insn, code->len);

	fprintf(stdout, "};\n");
	fprintf(stdout, "\n");

	if (c->expr_cnt) {
		for (i = 0; i < c->expr_cnt; i++) {
			struct unw_code *expr = c->expr + i;

			fprintf(stdout, "struct bpf_insn ");
			fprintf(stdout, "__attribute__((section(\"__unwind_data\"))) ");
			fprintf(stdout, "expr_insn_%d_%d[%d] = {\n", idx, i, expr->len);

			write_insn(expr->insn, expr->len);

			fprintf(stdout, "};\n");
			fprintf(stdout, "\n");

			fprintf(stdout, "struct du_expr ");
			fprintf(stdout, "__attribute__((section(\"__unwind_data\"))) ");
			fprintf(stdout, "expr_%d_%d = { %d, expr_insn_%d_%d };\n",
				idx, i, expr->len, idx, i);
			fprintf(stdout, "\n");
		}

		fprintf(stdout, "struct du_expr_array ");
		fprintf(stdout, "__attribute__((section(\"__unwind_data\"))) ");
		fprintf(stdout, "expr_array_%d = {\n", idx);
		fprintf(stdout, "	.len      = %d,\n", c->expr_cnt);
		fprintf(stdout, "	.expr = {\n");

		for (i = 0; i < c->expr_cnt; i++) {
			fprintf(stdout, "		&expr_%d_%d,\n", idx, i);
		}
		fprintf(stdout, "	}\n");

		fprintf(stdout, "};\n");
		fprintf(stdout, "\n");
	}

	fprintf(stdout, "struct du_frame ");
	fprintf(stdout, "__attribute__((section(\"__dunw_data\"))) ");
	fprintf(stdout, "frame_%d = {\n", idx);
	fprintf(stdout, "	.loc_start = (__u8 *) 0x%lx,\n", fde->loc_start);
	fprintf(stdout, "	.loc_end   = (__u8 *) 0x%lx,\n", fde->loc_end);
	fprintf(stdout, "	.len       = %d,\n", code->len);
	fprintf(stdout, "	.insn      = insn_%d,\n", idx);
	if (c->expr_cnt)
		fprintf(stdout, "	.expr      = &expr_array_%d,\n", idx);
	fprintf(stdout, "};\n");

	fprintf(stdout, "\n");

	fprintf(stdout, "struct du_frame* ");
	fprintf(stdout, "__attribute__((section(\"__dunw_frame\"))) ");
	fprintf(stdout, "frame_ptr_%d = &frame_%d;\n", idx, idx);

	fprintf(stdout, "\n");

	idx++;

	return 0;
}

/*
 * # R1 holds struct du_unwind
 *
 * mov R2, *R1   -> R2 holds IP
 * add R1, 8
 * add R3, R1    -> R3 holds loc_end
 * add R1, 8     -> R1 holds bottom of the state stack
 *
 * exit
 */

static struct unw_insn insn_unwind[] = {
	{ .bi = BPF_EMIT_CALL_UNWIND(), .cstr = "CALL UNWIND" },
};

static struct unw_insn insn_entry[] = {
	{ .bi = BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_1, 0),			.cstr = "REG_2  = [REG_1]"	},
	{ .bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, sizeof(unsigned long)),	.cstr = "REG_1 += 8"		},
	{ .bi = BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_1, 0),			.cstr = "REG_3  = [REG_1]"	},
	{ .bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, sizeof(unsigned long)),	.cstr = "REG_1 += 8"		},
	{ .bi = BPF_ALU64_REG(BPF_XOR, BPF_REG_0, BPF_REG_0),			.cstr = "REG_0  = 0"		},
};

static struct unw_insn insn_exit[] = {
	{ .bi = BPF_EXIT_INSN(),				.cstr = "RET" },
};

static struct unw_insn insn_next[] = {
	{ .bi = BPF_JMP_REG(BPF_JGT, BPF_REG_3, BPF_REG_2, 1),	.cstr = "IF (REG_2 > REG_3)"	},
	{ .bi = BPF_EXIT_INSN(),				.cstr = "   RET" },
};

static int emit_entry(struct unw_code *code)
{
	return add_code(code, insn_entry, ARRAY_SIZE(insn_entry));
}

static int emit_exit(struct unw_code *code)
{
	return add_code(code, insn_exit, ARRAY_SIZE(insn_exit));
}

int emit_debug(struct unw_code *code)
{
	return debug ? add_code(code, insn_unwind, ARRAY_SIZE(insn_unwind)) : 0;
}

int emit_next(struct unw_code *code)
{
	return add_code(code, insn_next, ARRAY_SIZE(insn_next));
}

static void cleanup(struct unw_code *code, struct unw_code *expr,
		    int expr_cnt, bool _free)
{
	int i;

	for (i = 0; i < expr_cnt; i++)
		clean_code(expr + i, _free);

	clean_code(code, _free);
}

static int fde_cb(struct unw_fde *fde)
{
	struct unw_cie *cie = fde->cie;
	struct unw_convert c = {
		.code	= &code_buf,
		.expr	= expr_buf,
		.fde	= fde,
	};

	if (emit_entry(c.code))
		return -1;

	if (emit_cfi(&c, &cie->frame))
		return -1;

	if (emit_cfi(&c, &fde->frame))
		return -1;

	if (emit_exit(c.code))
		return -1;

	if (write_frame(&c))
		return -1;

	cleanup(c.code, c.expr, c.expr_cnt, false);
	return 0;
}

static const char * const usage[] = {
	"convert <object>",
	NULL
};

static struct option options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_BOOLEAN('d', "debug", &debug, "include debug calls"),
	OPT_BOOLEAN('c', "comments", &comments, "output instructions comments"),
	OPT_END()
};

static int process_object(const char *file)
{
	unsigned long start, stop;
	Elf *elf;
	int fd;

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		pr_err("failed to open %s\n", file);
		return -1;
	}

	if (eh_frame(&elf, fd, &start, &stop)) {
		pr_err("failed to get .eh_frame section\n");
		return -1;
	}

	if (parse_fdes((u8 *) start, (u8 *) stop)) {
		pr_err("failed to parse FDEs\n");
		return -1;
	}

	fprintf(stdout, "/* Generated file (tools/dwarf/unwind/convert). */\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "#include <linux/dwarf_unwind.h>\n");
	fprintf(stdout, "\n");

	if (walk_fdes(fde_cb)) {
		pr_err("failed to walk FDEs\n");
		return -1;
	}

	cleanup(&code_buf, expr_buf, MAX_EXPR, true);

	elf_end(elf);
	close(fd);
	return 0;
}

int main(int argc, const char **argv)
{
	argc = parse_options(argc, argv, options, usage,
                            PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc != 1)
		usage_with_options(usage, options);

	return process_object(argv[0]);
}

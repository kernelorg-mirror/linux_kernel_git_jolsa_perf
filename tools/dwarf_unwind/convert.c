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
#include "parse.h"
#include "read.h"
#include "debug.h"
#include "eh_frame.h"
#include "bpf.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

struct unw_insn {
	struct bpf_insn  bi;
	char		*str;
	char		*astr;
};

struct code {
	struct unw_insn *insn;
	int len;
	int alloc;
};

static struct code code;
static bool comments;

#define CODE_ALLOC 100

static int add_code(struct code *code, struct unw_insn *insn, int len)
{
	int i;

	if (code->len + len >= code->alloc) {
		int alloc = code->alloc + CODE_ALLOC;
		struct unw_insn *insn;

		insn = realloc(code->insn, sizeof(*insn) * alloc);
		if (!insn)
			return -ENOMEM;

		code->insn  = insn;
		code->alloc = alloc;
	}

	memcpy(&code->insn[code->len], insn, sizeof(*insn) * len);
	code->len += len;
	return 0;
}

static int clean_code(struct code *code)
{
	int i;

	for (i = 0; i < code->len; i++)
		free(code->insn[i].astr);

	code->len = 0;
}

static int free_code(struct code *code)
{
	free(code->insn);
}


static int write_frame(struct unw_fde *fde, struct unw_insn *insn, int len)
{
	static int idx;
	int i, ret;

	fprintf(stdout, "struct bpf_insn ");
	fprintf(stdout, "__attribute__((section(\"__unwind_data\"))) ");
	fprintf(stdout, "insn_%d[%d] = {\n", idx, len);

	for (i = 0; i < len; i++) {
		struct bpf_insn *bi = &insn[i].bi;

		ret = fprintf(stdout, "	{ 0x%x, 0x%x, 0x%x, 0x%x, 0x%x },",
			      bi->code, bi->dst_reg, bi->src_reg, bi->off, bi->imm);

		if (comments) {
			char *s = insn[i].str ? insn[i].str : insn[i].astr;

			if (s) {
				int indent = 50 - ret;
				ret += fprintf(stdout, "%*s/* %s ", indent, " ", s);

				indent = 100 - ret;
				ret = fprintf(stdout, "%*s */", indent, " ");
			}
		}
		fprintf(stdout, "\n");
	}

	fprintf(stdout, "};\n");

	fprintf(stdout, "\n");

	fprintf(stdout, "struct du_frame ");
	fprintf(stdout, "__attribute__((section(\"__dunw_data\"))) ");
	fprintf(stdout, "frame_%d = {\n", idx);
	fprintf(stdout, "	.loc_start = (__u8 *) 0x%lx,\n", fde->loc_start);
	fprintf(stdout, "	.loc_end   = (__u8 *) 0x%lx,\n", fde->loc_end);
	fprintf(stdout, "	.len       = %d,\n", len);
	fprintf(stdout, "	.insn      = insn_%d,\n", idx);
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
 * mov R2, *R1   -> R2 holds IP
 * add R1, 8
 * add R3, R1    -> R3 holds loc_end
 * add R1, 8     -> R1 holds bottom of the state stack
 *
 * exit
 */

static struct unw_insn insn_unwind[] = {
	{ .bi = BPF_EMIT_CALL_UNWIND(), .str = "CALL UNWIND" },
};

static struct unw_insn insn_entry[] = {
	{ .bi = BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_1, 0),			.str = "REG_2 = [REG_1]"	},
	{ .bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, sizeof(unsigned long)),	.str = "REG_1 += 8"		},
	{ .bi = BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_1, 0),			.str = "REG_3 = [REG_1]"	},
	{ .bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, sizeof(unsigned long)),	.str = "REG_1 += 8"		},
};

static struct unw_insn insn_exit[] = {
	{ .bi = BPF_EXIT_INSN(), .str = "RET" },
};

static struct unw_insn insn_next[] = {
	{ .bi = BPF_JMP_REG(BPF_JGT, BPF_REG_3, BPF_REG_2, 1),	.str = "IF (REG_2 >= REG_3)" },
	{ .bi = BPF_EXIT_INSN(),				.str = "   RET" },
};

static int emit_entry(struct code *code)
{
	return add_code(code, insn_entry, ARRAY_SIZE(insn_entry));
}

static int emit_exit(struct code *code)
{
	return add_code(code, insn_exit, ARRAY_SIZE(insn_exit));
}

static int emit_debug(struct code *code)
{
	return add_code(code, insn_unwind, ARRAY_SIZE(insn_unwind));
}

static int set_reg(struct code *code, unsigned long reg, unsigned long val, unsigned long loc, int state_idx)
{
	unsigned int offset_val, offset_loc;
	struct unw_insn insn[10];
	char buf[100];

	memset(insn, 0, sizeof(insn));

	/*
	 * state->state_current[state_idx]
	 * rs->reg[reg].loc = loc;
	 * rs->reg[reg].val = val;
	 */

	offset_val = (state_idx * sizeof(struct du_state)) + offsetof(struct du_state, stack->reg[reg].val);
	offset_loc = (state_idx * sizeof(struct du_state)) + offsetof(struct du_state, stack->reg[reg].loc);

	snprintf(buf, 100, "REG_4 = 0x%lx", val);
	insn[0].astr = strdup(buf);

	snprintf(buf, 100, "[REG_1 + 0x%lx] = REG_4", offset_val);
	insn[2].astr = strdup(buf);

	snprintf(buf, 100, "[REG_1 + 0x%lx] = DU_LOCATION_MEMORY", offset_loc);
	insn[3].astr = strdup(buf);

	insn[0].bi = BPF_LD_IMM64_1(BPF_REG_4, val);
	insn[1].bi = BPF_LD_IMM64_2(BPF_REG_4, val);
	insn[2].bi = BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_4, offset_val),
	insn[3].bi = BPF_ST_MEM(BPF_B, BPF_REG_1, offset_loc, loc);

	return add_code(code, insn, 4);
}


static int restore(struct code *code, unsigned long reg, int state_idx, int state_init)
{
	unsigned int offset_val, offset_loc;
	unsigned int offset_init_val, offset_init_loc;
	struct unw_insn insn[10];
	char buf[100];

	memset(insn, 0, sizeof(insn));

	offset_val = (state_idx * sizeof(struct du_state)) + offsetof(struct du_state, stack->reg[reg].loc);
	offset_loc = (state_idx * sizeof(struct du_state)) + offsetof(struct du_state, stack->reg[reg].val);

	offset_init_val = (state_init * sizeof(struct du_state)) + offsetof(struct du_state, stack->reg[reg].loc);
	offset_init_loc = (state_init * sizeof(struct du_state)) + offsetof(struct du_state, stack->reg[reg].val);

	snprintf(buf, 100, "REG_4 = [REG_1 + 0x%lx]", offset_val);
	insn[0].astr = strdup(buf);

	snprintf(buf, 100, "[REG_1 + 0x%lx], REG_4", offset_init_val);
	insn[1].astr = strdup(buf);

	snprintf(buf, 100, "REG_4 = [REG_1 + 0x%lx]", offset_loc);
	insn[2].astr = strdup(buf);

	snprintf(buf, 100, "[REG_1 + 0x%lx], REG_4", offset_init_loc);
	insn[3].astr = strdup(buf);

	insn[0].bi = BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_1, offset_val);
	insn[1].bi = BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_4, offset_init_val);
	insn[2].bi = BPF_LDX_MEM(BPF_B, BPF_REG_4, BPF_REG_1, offset_loc);
	insn[3].bi = BPF_STX_MEM(BPF_B, BPF_REG_1, BPF_REG_4, offset_init_loc);

	return add_code(code, insn, 4);
}


#define DWARF_CFA_OPCODE_MASK	0xc0
#define DWARF_CFA_OPERAND_MASK	0x3f

enum {
	DW_CFA_advance_loc			= 0x40,
	DW_CFA_offset				= 0x80,
	DW_CFA_restore				= 0xc0,
	DW_CFA_nop				= 0x00,
	DW_CFA_set_loc				= 0x01,
	DW_CFA_advance_loc1			= 0x02,
	DW_CFA_advance_loc2			= 0x03,
	DW_CFA_advance_loc4			= 0x04,
	DW_CFA_offset_extended			= 0x05,
	DW_CFA_restore_extended			= 0x06,
	DW_CFA_undefined			= 0x07,
	DW_CFA_same_value			= 0x08,
	DW_CFA_register				= 0x09,
	DW_CFA_remember_state			= 0x0a,
	DW_CFA_restore_state			= 0x0b,
	DW_CFA_def_cfa				= 0x0c,
	DW_CFA_def_cfa_register			= 0x0d,
	DW_CFA_def_cfa_offset			= 0x0e,
	DW_CFA_def_cfa_expression		= 0x0f,
	DW_CFA_expression			= 0x10,
	DW_CFA_offset_extended_sf		= 0x11,
	DW_CFA_def_cfa_sf			= 0x12,
	DW_CFA_def_cfa_offset_sf		= 0x13,
	DW_CFA_val_expression			= 0x16,
	DW_CFA_lo_user				= 0x1c,
	DW_CFA_MIPS_advance_loc8		= 0x1d,
	DW_CFA_GNU_window_save			= 0x2d,
	DW_CFA_GNU_args_size			= 0x2e,
	DW_CFA_GNU_negative_offset_extended	= 0x2f,
	DW_CFA_hi_user				= 0x3c,
};

#define SETREG_EXPR(reg, loc, addr, len)	\
do {						\
} while (0)					\

static int emit_cfi_code(struct code *code, struct unw_fde *fde,
			 struct unw_frame *frame, int *_state_idx)
{
	struct unw_cie *cie = fde->cie;
	u8 *addr     = frame->icode;
	u8 *addr_end = frame->icode + frame->ilen;
	u8 *curr_ip  = fde->loc_start;
	u8 *last_ip  = fde->loc_start;
	u8 *end_ip   = fde->loc_end;
	struct unw_insn insn[10];
	int state_init, state_idx = *_state_idx;
	unsigned int offset_val, offset_loc;
	unsigned int offset_init_val, offset_init_loc;
	char buf[100];

	state_init = state_idx;

	while ((curr_ip <= end_ip) && (addr < addr_end)) {
		u8 op, operand, reg, val8;
		unsigned long val, len;
		u16 val16;
		u32 val32;

		memset(insn, 0, sizeof(insn));

		op = DU_READ(addr, u8, addr_end);

		/* TODO check operand */
		operand = (u8) -1;

		if (op & DWARF_CFA_OPCODE_MASK) {
			operand = op & DWARF_CFA_OPERAND_MASK;
			op &= ~DWARF_CFA_OPERAND_MASK;
		}

		switch (op) {
		case DW_CFA_advance_loc:
			insn[0].bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, operand * cie->align_code);

			snprintf(buf, 100, "REG_2 += 0x%lx", operand * cie->align_code);
			insn[0].astr = strdup(buf);

			if (add_code(code, insn, 1))
				return -1;

			curr_ip += operand * cie->align_code;
			break;

		case DW_CFA_advance_loc1:
			val8       = DU_READ(addr, u8, addr_end);
			insn[0].bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, val8 * cie->align_code);

			snprintf(buf, 100, "REG_2 += 0x%lx", val8 * cie->align_code);
			insn[0].astr = strdup(buf);

			if (add_code(code, insn, 1))
				return -1;

			curr_ip += val8 * cie->align_code;
			break;

		case DW_CFA_advance_loc2:
			val16      = DU_READ(addr, u16, addr_end);
			insn[0].bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, val16 * cie->align_code);

			snprintf(buf, 100, "REG_2 += 0x%lx", val16 * cie->align_code);
			insn[0].astr = strdup(buf);

			if (add_code(code, insn, 1))
				return -1;

			curr_ip += val16 * cie->align_code;
			break;

		case DW_CFA_advance_loc4:
			val32      = DU_READ(addr, u32, addr_end);
			insn[0].bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, val32 * cie->align_code);

			snprintf(buf, 100, "REG_2 += 0x%lx", val32 * cie->align_code);
			insn[0].astr = strdup(buf);

			if (add_code(code, insn, 1))
				return -1;

			curr_ip += val32 * cie->align_code;
			break;

		case DW_CFA_MIPS_advance_loc8:
			return -EINVAL;

		case DW_CFA_offset:
			val = DU_READ_ULEB128(addr, addr_end);
			val *= cie->align_data;

			if (set_reg(code, operand, val, DU_LOCATION_MEMORY, state_idx))
				return -1;

			break;

		case DW_CFA_offset_extended:
		case DW_CFA_offset_extended_sf:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);
			val *= cie->align_data;

			if (set_reg(code, reg, val, DU_LOCATION_MEMORY, state_idx))
				return -1;
			
			break;

		case DW_CFA_restore:
			reg = operand;

			if (restore(code, reg, state_idx, state_init))
				return -1;

			break;

		case DW_CFA_restore_extended:
			reg = DU_READ_ULEB128(addr, addr_end);

			if (restore(code, reg, state_idx, state_init))
				return -1;
			break;

		case DW_CFA_nop:
			break;

		case DW_CFA_set_loc:
			curr_ip = (u8 *) DU_READ_ENCODED_VALUE(addr, addr_end,
							       cie->encoding);

			snprintf(buf, 100, "REG_4 = 0x%lx", curr_ip);
			insn[0].astr = strdup(buf);

			insn[0].bi = BPF_LD_IMM64_1(BPF_REG_4, (unsigned long) curr_ip);
			insn[1].bi = BPF_LD_IMM64_2(BPF_REG_4, (unsigned long) curr_ip);

			if (add_code(code, insn, 2))
				return -1;
			break;

		case DW_CFA_undefined:
			reg = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, reg, 0, DU_LOCATION_UNDEF, state_idx))
				return -1;
			break;

		case DW_CFA_same_value:
			reg = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, reg, 0, DU_LOCATION_SAME, state_idx))
				return -1;
			break;

		case DW_CFA_register:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, reg, val, DU_LOCATION_REG, state_idx))
				return -1;
			break;

		case DW_CFA_remember_state:
			if ((state_idx + 1) >= DU_CFA_STACK_MAX) {
				return -EINVAL;
			}
			state_idx++;

			snprintf(buf, 100, "REG_0++");
			insn[0].astr = strdup(buf);

			insn[0].bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, 1);

			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_restore_state:
			if (!state_idx) {
				return -EINVAL;
			}

			state_idx--;

			snprintf(buf, 100, "REG_0--");
			insn[0].astr = strdup(buf);

			insn[0].bi = BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, 1);

			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_def_cfa:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, DU_REG_CFA_REG_COLUMN, reg, DU_LOCATION_REG, state_idx))
				return -1;

			if (set_reg(code, DU_REG_CFA_OFF_COLUMN, val, DU_LOCATION_VALUE, state_idx))
				return -1;

			break;

		case DW_CFA_def_cfa_sf:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_SLEB128(addr, addr_end);
			val *= cie->align_data;

			if (set_reg(code, DU_REG_CFA_REG_COLUMN, reg, DU_LOCATION_REG, state_idx))
				return -1;

			if (set_reg(code, DU_REG_CFA_OFF_COLUMN, val, DU_LOCATION_VALUE, state_idx))
				return -1;

			break;

		case DW_CFA_def_cfa_register:
			reg = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, DU_REG_CFA_REG_COLUMN, reg, DU_LOCATION_REG, state_idx))
				return -1;

			break;

		case DW_CFA_def_cfa_offset:
			val = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, DU_REG_CFA_OFF_COLUMN, val, DU_LOCATION_VALUE, state_idx))
				return -1;
			break;

		case DW_CFA_def_cfa_offset_sf:
			val = DU_READ_SLEB128(addr, addr_end);
			val *= cie->align_data;

			if (set_reg(code, DU_REG_CFA_OFF_COLUMN, val, DU_LOCATION_VALUE, state_idx))
				return -1;
			break;

		case DW_CFA_def_cfa_expression:
			len = DU_READ_ULEB128(addr, addr_end);

			fprintf(stderr, "PICA\n");

			SETREG_EXPR(DU_REG_CFA_REG_COLUMN,
				    DU_LOCATION_EXPR,
				    addr, len);

			addr += len;

			insn[0].astr = strdup("PICA");

			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_expression:
			reg = DU_READ_ULEB128(addr, addr_end);
			len = DU_READ_ULEB128(addr, addr_end);

			fprintf(stderr, "PICA\n");
			SETREG_EXPR(DU_REG_CFA_REG_COLUMN,
				    DU_LOCATION_EXPR,
				    addr, len);

			addr += len;

			insn[0].astr = strdup("PICA");

			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_val_expression:
			reg = DU_READ_ULEB128(addr, addr_end);
			len = DU_READ_ULEB128(addr, addr_end);

			fprintf(stderr, "PICA\n");
			SETREG_EXPR(DU_REG_CFA_REG_COLUMN,
				    DU_LOCATION_EXPR_VALUE,
				    addr, len);

			addr += len;

			insn[0].astr = strdup("PICA");

			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_GNU_negative_offset_extended:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);
			val *= -cie->align_data;

			if (set_reg(code, reg, val, DU_LOCATION_MEMORY, state_idx))
				return -1;

			break;

		case DW_CFA_GNU_window_save:
			/*
			 * This is a special CFA to handle all 16 windowed
			 * registers on SPARC.
			 */

		default:
			return -EINVAL;
		}

		add_code(code, insn_unwind, ARRAY_SIZE(insn_unwind));

		if (curr_ip != last_ip) {
			if (add_code(code, insn_next, ARRAY_SIZE(insn_next)))
				return -1;

			last_ip = curr_ip;
		}
	}

	*_state_idx = state_idx;
	return 0;
}

static int emit_code(struct code *code, struct unw_fde *fde)
{
	struct unw_cie *cie = fde->cie;
	int state_idx = 0;

	if (emit_cfi_code(code, fde, &cie->frame, &state_idx))
		return -1;

	if (emit_cfi_code(code, fde, &fde->frame, &state_idx))
		return -1;

	return 0;
}

int fde_cb(struct unw_fde *fde)
{
	if (emit_entry(&code))
		return -1;

	if (emit_code(&code, fde))
		return -1;

	if (emit_exit(&code))
		return -1;

	if (write_frame(fde, code.insn, code.len))
		return -1;

	clean_code(&code);
	return 0;
}

static const char * const usage[] = {
	"convert <object>",
	NULL
};

static struct option options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
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

	free_code(&code);

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

#include <stdio.h>
#include <stdlib.h>
#include <libelf.h>
#include <gelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/bpf.h>
#include <linux/unwind.h>
#include <asm/errno.h>
#include "parse.h"
#include "read.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

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

static int write_frame(struct du_fde *fde, struct bpf_insn *insn, int len)
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

	return 0;
}

#define BPF_LD_IMM64_1(DST, IMM)				\
	BPF_LD_IMM64_RAW_1(DST, 0, IMM)

#define BPF_LD_IMM64_2(DST, IMM)				\
	BPF_LD_IMM64_RAW_2(DST, 0, IMM)

#define BPF_LD_IMM64_RAW_1(DST, SRC, IMM)			\
	((struct bpf_insn) {					\
		.code  = BPF_LD | BPF_DW | BPF_IMM,		\
		.dst_reg = DST,					\
		.src_reg = SRC,					\
		.off   = 0,					\
		.imm   = (__u32) (IMM) })

#define BPF_LD_IMM64_RAW_2(DST, SRC, IMM)			\
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

#define BPF_LDX_MEM(SIZE, DST, SRC, OFF)			\
	((struct bpf_insn) {					\
		.code  = BPF_LDX | BPF_SIZE(SIZE) | BPF_MEM,	\
		.dst_reg = DST,					\
		.src_reg = SRC,					\
		.off   = OFF,					\
		.imm   = 0 })

#define BPF_MOV64_REG(DST, SRC)					\
	((struct bpf_insn) {					\
		.code  = BPF_ALU64 | BPF_MOV | BPF_X,		\
		.dst_reg = DST,					\
		.src_reg = SRC,					\
		.off   = 0,					\
		.imm   = 0 })

#define BPF_ALU64_IMM(OP, DST, IMM)				\
	((struct bpf_insn) {					\
		.code  = BPF_ALU64 | BPF_OP(OP) | BPF_K,	\
		.dst_reg = DST,					\
		.src_reg = 0,					\
		.off   = 0,					\
		.imm   = IMM })

#define BPF_JMP_IMM(OP, DST, IMM, OFF)				\
	((struct bpf_insn) {					\
		.code  = BPF_JMP | BPF_OP(OP) | BPF_K,		\
		.dst_reg = DST,					\
		.src_reg = 0,					\
		.off   = OFF,					\
		.imm   = IMM })

#define BPF_JMP_REG(OP, DST, SRC, OFF)				\
	((struct bpf_insn) {					\
		.code  = BPF_JMP | BPF_OP(OP) | BPF_X,		\
		.dst_reg = DST,					\
		.src_reg = SRC,					\
		.off   = OFF,					\
		.imm   = 0 })

#define BPF_ST_MEM(SIZE, DST, OFF, IMM)				\
	((struct bpf_insn) {					\
		.code  = BPF_ST | BPF_SIZE(SIZE) | BPF_MEM,	\
		.dst_reg = DST,					\
		.src_reg = 0,					\
		.off   = OFF,					\
		.imm   = IMM })

#define BPF_STX_MEM(SIZE, DST, SRC, OFF)			\
	((struct bpf_insn) {					\
		.code  = BPF_STX | BPF_SIZE(SIZE) | BPF_MEM,	\
		.dst_reg = DST,					\
		.src_reg = SRC,					\
		.off   = OFF,					\
		.imm   = 0 })

/*
 * mov R2, *R1   -> R2 holds IP
 * add R1, 8
 * add R3, R1    -> R3 holds loc_end
 * add R1, 8     -> R1 holds bottom of the state stack
 *
 * exit
 */

static struct bpf_insn insn_unwind[] = {
	BPF_EMIT_CALL_UNWIND(),
};

static struct bpf_insn insn_entry[] = {
	BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_1, 0),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, sizeof(unsigned long)),
	BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_1, 0),
	BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, sizeof(unsigned long)),
};

static struct bpf_insn insn_exit[] ={
	BPF_EXIT_INSN(),
};

static struct bpf_insn insn_next[] = {
	BPF_JMP_REG(BPF_JGE, BPF_REG_3, BPF_REG_4, 1),
	BPF_EXIT_INSN(),
};

struct code {
	struct bpf_insn *insn;
	int len;
	int alloc;
};

static struct code code;

#define CODE_ALLOC 100

static int add_code(struct code *code, struct bpf_insn *insn, int len)
{
	if (code->len + len >= code->alloc) {
		int alloc = code->alloc + CODE_ALLOC;
		struct bpf_insn *insn;

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
	code->len = 0;
}

static int free_code(struct code *code)
{
	free(code->insn);
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

static void setreg(struct du_state_regs *rs, unsigned long reg,
		   enum du_location loc, unsigned long val)
{
	rs->reg[reg].loc = loc;
	rs->reg[reg].val = val;
}

static void setreg_expr(struct du_state_regs *rs, unsigned long reg,
			enum du_location loc, u8 *addr, unsigned long len)
{
	rs->reg[reg].loc  = DU_LOCATION_EXPR;
	rs->reg[reg].expr = addr;
	rs->reg[reg].len  = len;
}

#define STATE_CURRENT (&state->state_current[state->cur])
#define STATE_INITIAL (&state->state_initial)

#define CHK_REG(reg)						\
do {								\
	if (reg > DU_REGS_NUM) {				\
		return -EINVAL;					\
	}							\
} while (0)

#define SETREG(reg, loc, val)			\
do {						\
} while (0)					\

#define SETREG_EXPR(reg, loc, addr, len)	\
do {						\
} while (0)					\


static int emit_cfi_code(struct code *code, struct du_fde *fde,
			 struct du_frame *frame)
{
	struct du_cie *cie = fde->cie;
	u8 *addr     = frame->icode;
	u8 *addr_end = frame->icode + frame->ilen;
	u8 *curr_ip  = fde->loc_start;
	u8 *end_ip   = fde->loc_end;
	struct bpf_insn insn[10];
	int state_idx = 0;
	unsigned int offset_val, offset_loc;

	while ((curr_ip <= end_ip) && (addr < addr_end)) {
		u8 op, operand, reg, val8;
		unsigned long val, len;
		u16 val16;
		u32 val32;

		op = DU_READ(addr, u8, addr_end);

		/* TODO check operand */
		operand = (u8) -1;

		if (op & DWARF_CFA_OPCODE_MASK) {
			operand = op & DWARF_CFA_OPERAND_MASK;
			op &= ~DWARF_CFA_OPERAND_MASK;
		}

		switch (op) {
		case DW_CFA_advance_loc:
			insn[0] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, operand * cie->align_code);

			/* curr_ip += operand * cie->align_code; */
			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_advance_loc1:
			val8    = DU_READ(addr, u8, addr_end);
			insn[0] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, val8 * cie->align_code);

			/* curr_ip += val8 * cie->align_code; */
			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_advance_loc2:
			val16   = DU_READ(addr, u16, addr_end);
			insn[0] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, val16 * cie->align_code);

			/* curr_ip += val16 * cie->align_code; */
			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_advance_loc4:
			val32   = DU_READ(addr, u32, addr_end);
			insn[0] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, val32 * cie->align_code);

			/* curr_ip += val32 * cie->align_code; */
			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_MIPS_advance_loc8:
			return -EINVAL;

		case DW_CFA_offset:
			val = DU_READ_ULEB128(addr, addr_end);
			val *= cie->align_data;

			/*
			 * state->state_current[state_idx]
			 * rs->reg[reg].loc = loc;
			 * rs->reg[reg].val = val;
			 */

			offset_val = (state_idx * sizeof(struct du_state)) + offsetof(struct du_state, stack->reg[operand].loc);
			offset_loc = (state_idx * sizeof(struct du_state)) + offsetof(struct du_state, stack->reg[operand].val);

			insn[0] = BPF_LD_IMM64_1(BPF_REG_4, val);
			insn[1] = BPF_LD_IMM64_2(BPF_REG_4, val);
			insn[2] = BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_4, offset_val),
			insn[3] = BPF_ST_MEM(BPF_DW, BPF_REG_1, offset_loc, DU_LOCATION_MEMORY);

			if (add_code(code, insn, 4))
				return -1;
			break;

		case DW_CFA_offset_extended:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);
			val *= cie->align_data;

			SETREG(reg, DU_LOCATION_MEMORY, val);
			break;

		case DW_CFA_offset_extended_sf:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_SLEB128(addr, addr_end);
			val *= cie->align_data;

			SETREG(reg, DU_LOCATION_MEMORY, val);
			break;

		case DW_CFA_restore:
			CHK_REG(operand);
			//STATE_CURRENT->reg[operand] = STATE_INITIAL->reg[operand];
			break;

		case DW_CFA_restore_extended:
			reg = DU_READ_ULEB128(addr, addr_end);
			CHK_REG(operand);

			//STATE_CURRENT->reg[reg] = STATE_INITIAL->reg[reg];
			break;

		case DW_CFA_nop:
			break;

		case DW_CFA_set_loc:
			curr_ip = (u8 *) DU_READ_ENCODED_VALUE(addr, addr_end,
							       cie->encoding);
			break;

		case DW_CFA_undefined:
			reg = DU_READ_ULEB128(addr, addr_end);
			SETREG(reg, DU_LOCATION_UNDEF, 0);
			break;

		case DW_CFA_same_value:
			reg = DU_READ_ULEB128(addr, addr_end);
			SETREG(reg, DU_LOCATION_SAME, 0);
			break;

		case DW_CFA_register:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);
			SETREG(reg, DU_LOCATION_REG, val);
			break;

		case DW_CFA_remember_state:
/*
			if ((state->cur + 1) >= DWARF_UNWIND_CFA_STACK_MAX) {
				return -EINVAL;
			}
			state->cur++;
*/
			break;

		case DW_CFA_restore_state:
/*
			if (!state->cur) {
				return -EINVAL;
			}

			state->cur--;
*/
			break;

		case DW_CFA_def_cfa:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);

			SETREG(DU_REG_CFA_REG_COLUMN,
			       DU_LOCATION_REG, reg);
			SETREG(DU_REG_CFA_OFF_COLUMN,
			       DU_LOCATION_VALUE, val);
			break;

		case DW_CFA_def_cfa_sf:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_SLEB128(addr, addr_end);
			val *= cie->align_data;

			SETREG(DU_REG_CFA_REG_COLUMN,
			       DU_LOCATION_REG, reg);
			SETREG(DU_REG_CFA_OFF_COLUMN,
			       DU_LOCATION_VALUE, val);
			break;

		case DW_CFA_def_cfa_register:
			reg = DU_READ_ULEB128(addr, addr_end);

			SETREG(DU_REG_CFA_REG_COLUMN,
			       DU_LOCATION_REG, reg);
			break;

		case DW_CFA_def_cfa_offset:
			val = DU_READ_ULEB128(addr, addr_end);

			SETREG(DU_REG_CFA_OFF_COLUMN,
			       DU_LOCATION_VALUE, val);
			break;

		case DW_CFA_def_cfa_offset_sf:
			val = DU_READ_SLEB128(addr, addr_end);
			val *= cie->align_data;

			SETREG(DU_REG_CFA_OFF_COLUMN,
			       DU_LOCATION_VALUE, val);
			break;

		case DW_CFA_def_cfa_expression:
			len = DU_READ_ULEB128(addr, addr_end);

			SETREG_EXPR(DU_REG_CFA_REG_COLUMN,
				    DU_LOCATION_EXPR,
				    addr, len);

			addr += len;
			break;

		case DW_CFA_expression:
			reg = DU_READ_ULEB128(addr, addr_end);
			len = DU_READ_ULEB128(addr, addr_end);

			SETREG_EXPR(DU_REG_CFA_REG_COLUMN,
				    DU_LOCATION_EXPR,
				    addr, len);

			addr += len;
			break;

		case DW_CFA_val_expression:
			reg = DU_READ_ULEB128(addr, addr_end);
			len = DU_READ_ULEB128(addr, addr_end);

			SETREG_EXPR(DU_REG_CFA_REG_COLUMN,
				    DU_LOCATION_EXPR_VALUE,
				    addr, len);

			addr += len;
			break;

		case DW_CFA_GNU_negative_offset_extended:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);
			val *= -cie->align_data;

			SETREG(reg, DU_LOCATION_MEMORY, val);
			break;

		case DW_CFA_GNU_window_save:
			/*
			 * This is a special CFA to handle all 16 windowed
			 * registers on SPARC.
			 */

		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int emit_code(struct code *code, struct du_fde *fde)
{
	struct du_cie *cie = fde->cie;

//	add_code(code, insn_unwind, ARRAY_SIZE(insn_unwind));

	if (emit_cfi_code(code, fde, &cie->frame))
		return -1;

	if (emit_cfi_code(code, fde, &fde->frame))
		return -1;

	return 0;
}

#if 0



#define STATE_CURRENT (&state->state_current[state->cur])
#define STATE_INITIAL (&state->state_initial)

#define CHK_REG(reg)						\
do {								\
	if (reg > DU_REGS_NUM) {				\
		return -EINVAL;					\
	}							\
} while (0)

#define SETREG(reg, loc, val)			\
do {						\
	CHK_REG(reg);				\
	setreg(STATE_CURRENT, reg, loc, val);	\
} while (0)					\

#define SETREG_EXPR(reg, loc, addr, len)	\
do {						\
	CHK_REG(reg);				\
	setreg_expr(STATE_CURRENT, reg, loc, addr, len);\
} while (0)					\







#include "internal.h"

int du_cfi(struct du_fde *fde, struct du_state *state,
	   unsigned long ip, struct du_frame *frame)
{
	struct du_cie *cie = fde->cie;
	u8 *addr = frame->icode;
	u8 *addr_end = frame->icode + frame->ilen;
	u8 *curr_ip = fde->loc_start;

	while ((curr_ip <= (u8 *) ip) && (addr < addr_end)) {
		u8 op, operand, reg, val8;
		unsigned long val, len;
		u16 val16;
		u32 val32;

		op = DU_READ(addr, u8, addr_end);

		/* TODO check operand */
		operand = (u8) -1;

		if (op & DWARF_CFA_OPCODE_MASK) {
			operand = op & DWARF_CFA_OPERAND_MASK;
			op &= ~DWARF_CFA_OPERAND_MASK;
		}

		switch (op) {
		case DW_CFA_advance_loc:
			curr_ip += operand * cie->align_code;
			DU_DEBUG_CFI("CFA_advance_loc to %p\n", curr_ip);
			break;

		case DW_CFA_advance_loc1:
			val8 = DU_READ(addr, u8, addr_end);
			curr_ip += val8 * cie->align_code;
			DU_DEBUG_CFI("CFA_advance_loc1 to %p\n", curr_ip);
			break;

		case DW_CFA_advance_loc2:
			val16 = DU_READ(addr, u16, addr_end);
			curr_ip += val16 * cie->align_code;
			DU_DEBUG_CFI("CFA_advance_loc2 to %p\n", curr_ip);
			break;

		case DW_CFA_advance_loc4:
			val32 = DU_READ(addr, u32, addr_end);
			curr_ip += val32 * cie->align_code;
			DU_DEBUG_CFI("CFA_advance_loc4 to %p\n", curr_ip);
			break;

		case DW_CFA_MIPS_advance_loc8:
			DU_DEBUG_CFI("FAILED DW_CFA_MIPS_advance_loc8\n");
			return -EINVAL;

		case DW_CFA_offset:
			val = DU_READ_ULEB128(addr, addr_end);
			val *= cie->align_data;

			SETREG(operand, DU_LOCATION_MEMORY, val);

			DU_DEBUG_CFI("CFA_offset r%u at cfa+%lu\n",
				     operand, val);
			break;

		case DW_CFA_offset_extended:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);
			val *= cie->align_data;

			SETREG(reg, DU_LOCATION_MEMORY, val);

			DU_DEBUG_CFI("CFA_offset_extended r%u at cf+0x%lx\n",
				     reg, val);
			break;

		case DW_CFA_offset_extended_sf:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_SLEB128(addr, addr_end);
			val *= cie->align_data;

			SETREG(reg, DU_LOCATION_MEMORY, val);

			DU_DEBUG_CFI("DW_CFA_offset_extended_sf r%u at cf+0x%lx\n",
				     reg, val);
			break;

		case DW_CFA_restore:
			CHK_REG(operand);
			STATE_CURRENT->reg[operand] = STATE_INITIAL->reg[operand];

			DU_DEBUG_CFI("CFA_restore r%u\n", operand);
			break;

		case DW_CFA_restore_extended:
			reg = DU_READ_ULEB128(addr, addr_end);
			CHK_REG(operand);

			STATE_CURRENT->reg[reg] = STATE_INITIAL->reg[reg];
			DU_DEBUG_CFI("CFA_restore_extended r%u\n", reg);
			break;

		case DW_CFA_nop:
			DU_DEBUG_CFI("DW_CFA_nop\n");
			break;

		case DW_CFA_set_loc:
			curr_ip = (u8 *) DU_READ_ENCODED_VALUE(addr, addr_end,
							       cie->encoding);

			DU_DEBUG_CFI("CFA_set_loc to %p\n", curr_ip);
			break;

		case DW_CFA_undefined:
			reg = DU_READ_ULEB128(addr, addr_end);
			SETREG(reg, DU_LOCATION_UNDEF, 0);

			DU_DEBUG_CFI("CFA_undefined r%u\n", reg);
			break;

		case DW_CFA_same_value:
			reg = DU_READ_ULEB128(addr, addr_end);
			SETREG(reg, DU_LOCATION_SAME, 0);

			DU_DEBUG_CFI("CFA_same_value r%u\n", reg);
			break;

		case DW_CFA_register:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);
			SETREG(reg, DU_LOCATION_REG, val);

			DU_DEBUG_CFI("CFA_register r%u to r%lu\n", reg, val);
			break;

		case DW_CFA_remember_state:
			if ((state->cur + 1) >= DWARF_UNWIND_CFA_STACK_MAX) {
				DU_DEBUG_CFI("FAILED stack top reached\n");
				return -EINVAL;
			}

			DU_DEBUG_CFI("CFA_remember_state %d\n", state->cur);
			state->cur++;
			break;

		case DW_CFA_restore_state:
			if (!state->cur) {
				DU_DEBUG_CFI("FAILED stack underflow\n");
				return -EINVAL;
			}

			state->cur--;
			DU_DEBUG_CFI("DW_CFA_restore_state %d\n", state->cur);
			break;

		case DW_CFA_def_cfa:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);

			SETREG(DU_REG_CFA_REG_COLUMN,
			       DU_LOCATION_REG, reg);
			SETREG(DU_REG_CFA_OFF_COLUMN,
			       DU_LOCATION_VALUE, val);

			DU_DEBUG_CFI("CFA_def_cfa r%u+0x%lx\n", reg, val);
			break;

		case DW_CFA_def_cfa_sf:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_SLEB128(addr, addr_end);
			val *= cie->align_data;

			SETREG(DU_REG_CFA_REG_COLUMN,
			       DU_LOCATION_REG, reg);
			SETREG(DU_REG_CFA_OFF_COLUMN,
			       DU_LOCATION_VALUE, val);

			DU_DEBUG_CFI("CFA_def_cfa_sf r%u+0x%lx\n", reg, val);
			break;

		case DW_CFA_def_cfa_register:
			reg = DU_READ_ULEB128(addr, addr_end);

			SETREG(DU_REG_CFA_REG_COLUMN,
			       DU_LOCATION_REG, reg);

			DU_DEBUG_CFI("CFA_def_cfa_register r%u\n", reg);
			break;

		case DW_CFA_def_cfa_offset:
			val = DU_READ_ULEB128(addr, addr_end);

			SETREG(DU_REG_CFA_OFF_COLUMN,
			       DU_LOCATION_VALUE, val);

			DU_DEBUG_CFI("CFA_def_cfa_offset 0x%lx\n", val);
			break;

		case DW_CFA_def_cfa_offset_sf:
			val = DU_READ_SLEB128(addr, addr_end);
			val *= cie->align_data;

			SETREG(DU_REG_CFA_OFF_COLUMN,
			       DU_LOCATION_VALUE, val);

			DU_DEBUG_CFI("CFA_def_cfa_offset_sf 0x%lx\n", val);
			break;

		case DW_CFA_def_cfa_expression:
			len = DU_READ_ULEB128(addr, addr_end);

			SETREG_EXPR(DU_REG_CFA_REG_COLUMN,
				    DU_LOCATION_EXPR,
				    addr, len);

			addr += len;
			DU_DEBUG_CFI("CFA_def_cfa_expr @ %p [%lu bytes]\n",
				     addr, len);
			break;

		case DW_CFA_expression:
			reg = DU_READ_ULEB128(addr, addr_end);
			len = DU_READ_ULEB128(addr, addr_end);

			SETREG_EXPR(DU_REG_CFA_REG_COLUMN,
				    DU_LOCATION_EXPR,
				    addr, len);

			addr += len;

			DU_DEBUG_CFI("CFA_expression r%u @ %p [%lu bytes]\n",
				     reg, addr, len);
			break;

		case DW_CFA_val_expression:
			reg = DU_READ_ULEB128(addr, addr_end);
			len = DU_READ_ULEB128(addr, addr_end);

			SETREG_EXPR(DU_REG_CFA_REG_COLUMN,
				    DU_LOCATION_EXPR_VALUE,
				    addr, len);

			addr += len;

			DU_DEBUG_CFI("CFA_expression r%u @ %p [%lu bytes]\n",
				     reg, addr, len);
			break;

		case DW_CFA_GNU_negative_offset_extended:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);
			val *= -cie->align_data;

			SETREG(reg, DU_LOCATION_MEMORY, val);

			DU_DEBUG_CFI("CFA_GNU_negative_offset_extended cfa+0x%lx\n", val);
			break;

		case DW_CFA_GNU_window_save:
			/*
			 * This is a special CFA to handle all 16 windowed
			 * registers on SPARC.
			 */

		default:
			return -EINVAL;
		}
	}

	return 0;
}
#endif

























static int emit_entry(struct code *code)
{
	if (add_code(code, insn_entry, ARRAY_SIZE(insn_entry)))
		return -1;

	return 0;
}

int fde_cb(struct du_fde *fde)
{
	if (emit_entry(&code))
		return -1;

	if (emit_code(&code, fde))
		return -1;

	if (add_code(&code, insn_exit, ARRAY_SIZE(insn_exit)))
		return -1;

	if (write_frame(fde, code.insn, code.len))
		return -1;

	clean_code(&code);
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

	free_code(&code);

	elf_end(elf);
	close(fd);
	return 0;
}

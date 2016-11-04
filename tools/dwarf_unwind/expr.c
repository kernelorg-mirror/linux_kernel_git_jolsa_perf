#include <linux/types.h>
#include <linux/dwarf_unwind.h>
#include <asm/bug.h>
#include <asm/errno.h>
#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "read.h"
#include "code.h"
#include "bpf.h"
#include "convert.h"

#define NUM_OPERANDS(signature)	(((signature) >> 6) & 0x3)
#define OPND1_TYPE(signature)	(((signature) >> 3) & 0x7)
#define OPND2_TYPE(signature)	(((signature) >> 0) & 0x7)

#define OPND_SIGNATURE(n, t1, t2) (((n) << 6) | ((t1) << 3) | ((t2) << 0))
#define OPND1(t1)	OPND_SIGNATURE(1, t1, 0)
#define OPND2(t1, t2)	OPND_SIGNATURE(2, t1, t2)

#define VAL8	0x0
#define VAL16	0x1
#define VAL32	0x2
#define VAL64	0x3
#define ULEB128	0x4
#define SLEB128	0x5
#define OFFSET	0x6	/* 32-bit offset for 32-bit DWARF, 64-bit otherwise */
#define ADDR	0x7	/* Machine address.  */

enum {
	DW_OP_addr			= 0x03,
	DW_OP_deref			= 0x06,
	DW_OP_const1u			= 0x08,
	DW_OP_const1s			= 0x09,
	DW_OP_const2u			= 0x0a,
	DW_OP_const2s			= 0x0b,
	DW_OP_const4u			= 0x0c,
	DW_OP_const4s			= 0x0d,
	DW_OP_const8u			= 0x0e,
	DW_OP_const8s			= 0x0f,
	DW_OP_constu			= 0x10,
	DW_OP_consts			= 0x11,
	DW_OP_dup			= 0x12,
	DW_OP_drop			= 0x13,
	DW_OP_over			= 0x14,
	DW_OP_pick			= 0x15,
	DW_OP_swap			= 0x16,
	DW_OP_rot			= 0x17,
	DW_OP_xderef			= 0x18,
	DW_OP_abs			= 0x19,
	DW_OP_and			= 0x1a,
	DW_OP_div			= 0x1b,
	DW_OP_minus			= 0x1c,
	DW_OP_mod			= 0x1d,
	DW_OP_mul			= 0x1e,
	DW_OP_neg			= 0x1f,
	DW_OP_not			= 0x20,
	DW_OP_or			= 0x21,
	DW_OP_plus			= 0x22,
	DW_OP_plus_uconst		= 0x23,
	DW_OP_shl			= 0x24,
	DW_OP_shr			= 0x25,
	DW_OP_shra			= 0x26,
	DW_OP_xor			= 0x27,
	DW_OP_skip			= 0x2f,
	DW_OP_bra			= 0x28,
	DW_OP_eq			= 0x29,
	DW_OP_ge			= 0x2a,
	DW_OP_gt			= 0x2b,
	DW_OP_le			= 0x2c,
	DW_OP_lt			= 0x2d,
	DW_OP_ne			= 0x2e,
	DW_OP_lit0			= 0x30,
	DW_OP_lit1,  DW_OP_lit2,  DW_OP_lit3,  DW_OP_lit4,  DW_OP_lit5,
	DW_OP_lit6,  DW_OP_lit7,  DW_OP_lit8,  DW_OP_lit9,  DW_OP_lit10,
	DW_OP_lit11, DW_OP_lit12, DW_OP_lit13, DW_OP_lit14, DW_OP_lit15,
	DW_OP_lit16, DW_OP_lit17, DW_OP_lit18, DW_OP_lit19, DW_OP_lit20,
	DW_OP_lit21, DW_OP_lit22, DW_OP_lit23, DW_OP_lit24, DW_OP_lit25,
	DW_OP_lit26, DW_OP_lit27, DW_OP_lit28, DW_OP_lit29, DW_OP_lit30,
	DW_OP_lit31,
	DW_OP_reg0			= 0x50,
	DW_OP_reg1,  DW_OP_reg2,  DW_OP_reg3,  DW_OP_reg4,  DW_OP_reg5,
	DW_OP_reg6,  DW_OP_reg7,  DW_OP_reg8,  DW_OP_reg9,  DW_OP_reg10,
	DW_OP_reg11, DW_OP_reg12, DW_OP_reg13, DW_OP_reg14, DW_OP_reg15,
	DW_OP_reg16, DW_OP_reg17, DW_OP_reg18, DW_OP_reg19, DW_OP_reg20,
	DW_OP_reg21, DW_OP_reg22, DW_OP_reg23, DW_OP_reg24, DW_OP_reg25,
	DW_OP_reg26, DW_OP_reg27, DW_OP_reg28, DW_OP_reg29, DW_OP_reg30,
	DW_OP_reg31,
	DW_OP_breg0			= 0x70,
	DW_OP_breg1,  DW_OP_breg2,  DW_OP_breg3,  DW_OP_breg4,  DW_OP_breg5,
	DW_OP_breg6,  DW_OP_breg7,  DW_OP_breg8,  DW_OP_breg9,  DW_OP_breg10,
	DW_OP_breg11, DW_OP_breg12, DW_OP_breg13, DW_OP_breg14, DW_OP_breg15,
	DW_OP_breg16, DW_OP_breg17, DW_OP_breg18, DW_OP_breg19, DW_OP_breg20,
	DW_OP_breg21, DW_OP_breg22, DW_OP_breg23, DW_OP_breg24, DW_OP_breg25,
	DW_OP_breg26, DW_OP_breg27, DW_OP_breg28, DW_OP_breg29, DW_OP_breg30,
	DW_OP_breg31,
	DW_OP_regx			= 0x90,
	DW_OP_fbreg			= 0x91,
	DW_OP_bregx			= 0x92,
	DW_OP_piece			= 0x93,
	DW_OP_deref_size		= 0x94,
	DW_OP_xderef_size		= 0x95,
	DW_OP_nop			= 0x96,
	DW_OP_push_object_address	= 0x97,
	DW_OP_call2			= 0x98,
	DW_OP_call4			= 0x99,
	DW_OP_call_ref			= 0x9a,
	DW_OP_lo_user			= 0xe0,
	DW_OP_hi_user			= 0xff
};

static uint8_t operands[256] =
{
	[DW_OP_addr] =		OPND1 (ADDR),
	[DW_OP_const1u] =	OPND1 (VAL8),
	[DW_OP_const1s] =	OPND1 (VAL8),
	[DW_OP_const2u] =	OPND1 (VAL16),
	[DW_OP_const2s] =	OPND1 (VAL16),
	[DW_OP_const4u] =	OPND1 (VAL32),
	[DW_OP_const4s] =	OPND1 (VAL32),
	[DW_OP_const8u] =	OPND1 (VAL64),
	[DW_OP_const8s] =	OPND1 (VAL64),
	[DW_OP_pick] =		OPND1 (VAL8),
	[DW_OP_plus_uconst] =	OPND1 (ULEB128),
	[DW_OP_skip] =		OPND1 (VAL16),
	[DW_OP_bra] =		OPND1 (VAL16),
	[DW_OP_breg0 +  0] =	OPND1 (SLEB128),
	[DW_OP_breg0 +  1] =	OPND1 (SLEB128),
	[DW_OP_breg0 +  2] =	OPND1 (SLEB128),
	[DW_OP_breg0 +  3] =	OPND1 (SLEB128),
	[DW_OP_breg0 +  4] =	OPND1 (SLEB128),
	[DW_OP_breg0 +  5] =	OPND1 (SLEB128),
	[DW_OP_breg0 +  6] =	OPND1 (SLEB128),
	[DW_OP_breg0 +  7] =	OPND1 (SLEB128),
	[DW_OP_breg0 +  8] =	OPND1 (SLEB128),
	[DW_OP_breg0 +  9] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 10] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 11] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 12] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 13] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 14] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 15] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 16] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 17] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 18] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 19] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 20] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 21] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 22] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 23] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 24] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 25] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 26] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 27] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 28] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 29] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 30] =	OPND1 (SLEB128),
	[DW_OP_breg0 + 31] =	OPND1 (SLEB128),
	[DW_OP_regx] =		OPND1 (ULEB128),
	[DW_OP_fbreg] =		OPND1 (SLEB128),
	[DW_OP_bregx] =		OPND2 (ULEB128, SLEB128),
	[DW_OP_piece] =		OPND1 (ULEB128),
	[DW_OP_deref_size] =	OPND1 (VAL8),
	[DW_OP_xderef_size] =	OPND1 (VAL8),
	[DW_OP_call2] =		OPND1 (VAL16),
	[DW_OP_call4] =		OPND1 (VAL32),
	[DW_OP_call_ref] =	OPND1 (OFFSET)
};

static int fix_type(int type)
{
	if (type != ADDR)
		return type;

	switch (sizeof(unsigned long)) {
	case 4: return VAL32;
	case 8: return VAL64;
	}

	WARN_ONCE(1, "bug");
}

/*
 * Using macro so the READ_OPERAND can exit the main function
 * any time error is hit like DU_READ* macros.
 */
#define READ_OPERAND(addr, type, end) ({			\
	unsigned long __val = 0;				\
	u8 __type = fix_type(type);				\
								\
	switch (__type) {					\
	case VAL8:						\
		__val = (u8) DU_READ(addr, u8, addr_end);	\
		break;						\
								\
	case VAL16:						\
		__val = (u16) DU_READ(addr, u16, addr_end);	\
		break;						\
								\
	case VAL32:						\
		__val = (u32) DU_READ(addr, u32, addr_end);	\
		break;						\
								\
	case VAL64:						\
		__val = (u64) DU_READ(addr, u64, addr_end);	\
		break;						\
								\
	case ULEB128:						\
		__val = DU_READ_ULEB128(addr, addr_end);	\
		break;						\
								\
	case SLEB128:						\
		__val = DU_READ_ULEB128(addr, addr_end);	\
		break;						\
								\
	default:						\
		pr_debug("unexpected operand type %d\n",	\
			      __type);				\
		return -EINVAL;					\
	}							\
	__val;							\
})

#define MAX_EXPR_STACK_SIZE	64

#define PUSH(v)						\
do {							\
	if (sp == MAX_EXPR_STACK_SIZE) {		\
		pr_debug("stack overflow\n");	\
		return -EINVAL;				\
	}						\
	stack[sp++] = (v);				\
} while (0)

#define POP()						\
({							\
	if (!sp) {					\
		pr_debug("stack underflow\n");	\
		return -EINVAL;				\
	}						\
	stack[--sp];					\
})

# define PICK(n)					\
({							\
	unsigned int __index = sp - 1 - (n);		\
	if (__index >= MAX_EXPR_STACK_SIZE) {		\
		pr_debug("out-of-stack pick\n");	\
		return -EINVAL;				\
	}						\
	stack[__index];					\
})

#define CHK_REG(reg)						\
do {								\
	if (reg > DU_REGS_NUM) {				\
		pr_debug("Invalid register number %d\n",	\
			     reg);				\
		return -EINVAL;					\
	}							\
} while (0)

#if 0
#define GETREG(r) ({	\
	CHK_REG(r);	\
	regs->reg[r];	\
})
#else
#define GETREG(r) 0
#endif

#define CHK_ADDR(addr)

bool is_big_endian(void)
{
#ifdef __LITTLE_ENDIAN
	return false;
#else
	return true;
#endif
}

/* TODO check this!!! */
#define sword(arg) arg

/*
 * push_reg(reg):
 *   sub  R10, 8
 *   mov *R10, reg
 */
static int push_reg(struct unw_code *code, int reg)
{
	struct unw_insn insn[] = {
		{ .bi = BPF_ALU64_IMM(BPF_SUB, BPF_REG_10, sizeof(unsigned long)), .cstr = "SUB  R10,8" },
		{ .bi = BPF_STX_MEM(BPF_DW, BPF_REG_10, reg, 0) },
	};
	char buf[100];

	snprintf(buf, 100, "MOV  [R10],R%d", reg - BPF_REG_0);
	insn[1].astr = strdup(buf);

	return add_code(code, insn, ARRAY_SIZE(insn));
}

/*
 * push_val(val)
 *   mov R3, val
 *   push_reg(R4)
 */
static int push_val(struct unw_code *code, unsigned long val)
{
	struct unw_insn insn[] = {
		{ .bi = BPF_MOV64_IMM(BPF_REG_3, val), .cstr = "MOV  R3,val" },
	};

	if (add_code(code, insn, ARRAY_SIZE(insn)))
		return -1;

	return push_reg(code, BPF_REG_3);
}

/*
 * pop_reg(reg)
 *   mov reg, [R10]
 *   sub R10, 8
 */
static int pop_reg(struct unw_code *code, int reg)
{
	struct unw_insn insn[] = {
		{ .bi = BPF_LDX_MEM(BPF_DW, BPF_REG_10, reg, 0),					},
		{ .bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_10, sizeof(unsigned long)), .cstr = "ADD  R10,8"	},
	};
	char buf[100];

	snprintf(buf, 100, "MOV  R%d, [R10]", reg - BPF_REG_0);
	insn[0].astr = strdup(buf);

	return add_code(code, insn, ARRAY_SIZE(insn));
}

/*
 * pick_reg(reg, index)
 *   mov reg, [R10]
 */
static int pick_reg(struct unw_code *code, int reg, int idx)
{
	int offset = idx * 8;
	struct unw_insn insn = {
		.bi = BPF_LDX_MEM(BPF_DW, BPF_REG_10, reg, offset),
	};
	char buf[100];

	snprintf(buf, 100, "MOV  R%d, [R10 + %d]", reg - BPF_REG_0, offset);
	insn.astr = strdup(buf);

	return add_code(code, &insn, 1);
}

/*
 * get_reg(idx, reg)
 * mov reg, [R1 + (reg offset from idx)]
 */
static int get_reg(struct unw_code *code, int idx, int reg)
{
	unsigned int offset_val = offsetof(struct du_regs, reg[idx]);
	struct unw_insn insn;
	char buf[100];

	snprintf(buf, 100, "MOV  R%d, [R1 + 0x%lx]", reg - BPF_REG_0, offset_val);

	memset(&insn, 0, sizeof(insn));
	insn.astr = strdup(buf);
	insn.bi   = BPF_LDX_MEM(BPF_DW, reg, BPF_REG_1, offset_val);

	return add_code(code, &insn, 1);
}

static int add_reg(struct unw_code *code, int reg, unsigned int val)
{
	struct unw_insn insn;
	char buf[100];

	snprintf(buf, 100, "ADD  R%d,0x%lx", reg - BPF_REG_0, val);

	memset(&insn, 0, sizeof(insn));
	insn.astr = strdup(buf);
	insn.bi   = BPF_ALU64_IMM(BPF_ADD, reg, val);

	return add_code(code, &insn, 1);
}

static int deref_reg(struct unw_code *code, int reg_dst, int reg_src, int size)
{
	struct unw_insn insn;
	char buf[100];
	int bpf_size = 0;

	switch (size) {
	case 1:       bpf_size = BPF_B;  break;
	case 2:       bpf_size = BPF_H;  break;
	case 3 ... 4: bpf_size = BPF_W;  break;
	case 5 ... 8: bpf_size = BPF_DW; break;
	default: return -1;
	};

	snprintf(buf, 100, "MOV  R%d, [R%d] (size %d) KRAVA", reg_dst - BPF_REG_0, reg_src - BPF_REG_0, size);

	memset(&insn, 0, sizeof(insn));
	insn.astr = strdup(buf);
	insn.bi   = BPF_LDX_MEM(bpf_size, reg_src, reg_dst, 0);

	return add_code(code, &insn, 1);
}

static int abs_reg(struct unw_code *code, int reg)
{
	struct unw_insn insn[] = {
		{ .bi = BPF_ALU64_REG(BPF_XOR, BPF_REG_4, BPF_REG_4),	.cstr = "XOR  R4, R4"		},
		{ .bi = BPF_JMP_REG(BPF_JGT, BPF_REG_4, BPF_REG_3, 1),	.cstr = "IF (REG_2 > REG_3"	},
		{ .bi = BPF_ALU64_REG(BPF_NEG, BPF_REG_3, 0),		.cstr = "NEG  R3"		},
	};
	char buf[100];

	snprintf(buf, 100, "IF   (REG_4 > %d)", reg - BPF_REG_0);
	insn[1].astr = strdup(buf);

	snprintf(buf, 100, "NEG   %d", reg - BPF_REG_0);
	insn[1].astr = strdup(buf);

	return add_code(code, insn, ARRAY_SIZE(insn));
}

static int alu_reg(struct unw_code *code, int op, int reg_dst, int reg_src)
{
	struct unw_insn insn = {
		.bi = BPF_ALU64_REG(op, reg_dst, reg_src),
	};
	char buf[100];
	char *ops;

	switch (op) {
	case BPF_ADD: ops = "ADD"; break;
	case BPF_SUB: ops = "SUB"; break;
	case BPF_MUL: ops = "MUL"; break;
	case BPF_DIV: ops = "DIV"; break;
	case BPF_OR:  ops = "OR "; break;
	case BPF_AND: ops = "AND"; break;
	case BPF_LSH: ops = "LSH"; break;
	case BPF_RSH: ops = "RSH"; break;
	case BPF_NEG: ops = "NEG"; break;
	case BPF_MOD: ops = "MOD"; break;
	case BPF_XOR: ops = "XOR"; break;
	default: return -1;
	};

	snprintf(buf, 100, "%s  %d, %d", ops, reg_dst - BPF_REG_0, reg_src - BPF_REG_0);
	insn.astr = strdup(buf);

	return add_code(code, &insn, 1);
}

static int push_cmp(struct unw_code *code, int reg, int op, int reg_1, int reg_2)
{
	struct unw_insn insn[] = {
		{ .bi = BPF_ALU64_REG(BPF_XOR, reg, reg), },
		{ .bi = BPF_JMP_REG(op, reg_1, reg_2, 1), },
		{ .bi = BPF_ALU64_IMM(BPF_ADD, reg,   1), },
	};
	char buf[100];
	char *ops;

	switch (op) {
	case BPF_ADD: ops = "ADD"; break;
	default: return -1;
	};

	snprintf(buf, 100, "MOV  R%d, R%d", reg - BPF_REG_0, reg - BPF_REG_0);
	insn[0].astr = strdup(buf);

	snprintf(buf, 100, "IF (R%d %s R%d)", reg_1 - BPF_REG_0, reg_2 - BPF_REG_0);
	insn[0].astr = strdup(buf);

	return add_code(code, insn, ARRAY_SIZE(insn));
}

/*
 * # R1  holds struct du_int_expr
 * # R10 holds stack pointer
 *
 * mov R2, *R1   -> R2 holds register value
 * add R1, 8     -> R1 holds struct du_regs pointer
 *
 * exit
 */
int _emit_expr(struct unw_code *code, u8 *addr, unsigned long len)
{
	struct unw_insn insn[10];
	u8 *addr_end = addr + len;

	if (push_reg(code, BPF_REG_2))
		return -1;

	emit_debug(code);

	while (addr < addr_end) {
		u8 opcode, opsign, lit, reg;
		unsigned long op1 = 0, op2 = 0;
		unsigned long tmp1, tmp2, tmp3;

		opcode = DU_READ(addr, u8, addr_end);
		opsign = operands[opcode];

		if ((NUM_OPERANDS(opsign) > 0)) {
			op1 = READ_OPERAND(addr, OPND1_TYPE(opsign),
					   addr_end);

			if (NUM_OPERANDS(opsign) > 1)
				op2 = READ_OPERAND(addr, OPND2_TYPE(opsign),
						   addr_end);
		}

		switch (opcode) {
		case DW_OP_lit0 ... DW_OP_lit31:
			lit = opcode - DW_OP_lit0;

			if (push_val(code, lit))
				return -1;

			pr_debug("OP_lit(%d)\n", lit);
			break;

		case DW_OP_breg0 ... DW_OP_breg31:
			reg = opcode - DW_OP_breg0;

			if (get_reg(code, reg, BPF_REG_3))
				return -1;

			if (op1 && add_reg(code, BPF_REG_3, op1))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			pr_debug("OP_breg(r%d,0x%lx)\n", reg, op1);
			break;

		case DW_OP_bregx:
			reg = (u8) op1;

			if (get_reg(code, reg, BPF_REG_3))
				return -1;

			if (op2 && add_reg(code, BPF_REG_3, op2))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			pr_debug("OP_bregx(r%d,0x%lx)\n", reg, op2);
			break;

		case DW_OP_reg0 ... DW_OP_reg31:
			reg = opcode - DW_OP_reg0;

			/* WTF ?!? */
			if (push_val(code, reg))
				return -1;

			pr_debug("OP_reg(r%d)\n", reg);
			break;

		case DW_OP_regx:
			reg = (u8) op1;

			/* WTF ?!? */
			if (push_val(code, reg))
				return -1;

			pr_debug("OP_regx(r%d)\n", reg);
			break;

		case DW_OP_addr:
		case DW_OP_const1u:
		case DW_OP_const2u:
		case DW_OP_const4u:
		case DW_OP_const8u:
		case DW_OP_constu:
		case DW_OP_const8s:
		case DW_OP_consts:
			if (push_val(code, op1))
				return -1;

			pr_debug("OP_const(0x%lx)\n", op1);
			break;

		case DW_OP_const1s:
			if (op1 & 0x80)
				op1 |= ((unsigned long) -1) << 8;

			if (push_val(code, op1))
				return -1;

			pr_debug("OP_const1s(%ld)\n", op1);
			break;

		case DW_OP_const2s:
			if (op1 & 0x8000)
				op1 |= ((unsigned long) -1) << 16;

			if (push_val(code, op1))
				return -1;

			pr_debug("OP_const2s(%ld)\n", op1);
			break;

		case DW_OP_const4s:
			if (op1 & 0x8000)
				op1 |= (((unsigned long) -1) << 16) << 16;

			if (push_val(code, op1))
				return -1;

			pr_debug("OP_const4s(%ld)\n", op1);
			break;

		case DW_OP_deref:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (deref_reg(code, BPF_REG_4, BPF_REG_3, 8))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_deref\n");
			break;

		case DW_OP_deref_size:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (deref_reg(code, BPF_REG_4, BPF_REG_3, op1))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_deref\n");
			break;

		case DW_OP_dup:
			if (pick_reg(code, BPF_REG_3, 0))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			pr_debug("OP_dup\n");
			break;

		case DW_OP_drop:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			pr_debug("OP_drop\n");
			break;

		case DW_OP_pick:
			if (pick_reg(code, BPF_REG_3, op1))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			pr_debug("OP_pick(%lu)\n", op1);
			break;

		case DW_OP_over:
			if (pick_reg(code, BPF_REG_3, 1))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			pr_debug("OP_over\n");
			break;

		case DW_OP_swap:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_swap\n");
			break;

		case DW_OP_rot:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (pop_reg(code, BPF_REG_5))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			if (push_reg(code, BPF_REG_5))
				return -1;

			pr_debug("OP_rot\n");
			break;

		case DW_OP_abs:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (abs_reg(code, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			pr_debug("OP_abs\n");
			break;

		case DW_OP_and:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (alu_reg(code, BPF_AND, BPF_REG_3, BPF_REG_4))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			pr_debug("OP_and\n");
			break;

		case DW_OP_div:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (alu_reg(code, BPF_DIV, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_div\n");
			break;

		case DW_OP_minus:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (alu_reg(code, BPF_SUB, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_minus\n");
			break;

		case DW_OP_mod:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (alu_reg(code, BPF_MOD, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_mod\n");
			break;

		case DW_OP_mul:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (alu_reg(code, BPF_MUL, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_mul\n");
			break;

		case DW_OP_not:
			/* FUCK.. no ~ operand in BPF */

		case DW_OP_neg:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (alu_reg(code, BPF_NEG, BPF_REG_3, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			pr_debug("OP_neg\n");
			break;

		case DW_OP_or:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (alu_reg(code, BPF_OR, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_or\n");
			break;

		case DW_OP_plus:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (alu_reg(code, BPF_ADD, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_plus\n");
			break;

		case DW_OP_plus_uconst:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (op1 && add_reg(code, BPF_REG_3, op1))
				return -1;

			if (push_reg(code, BPF_REG_3))
				return -1;

			pr_debug("OP_plus_uconst(%lu)\n", op1);
			break;

		case DW_OP_shl:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (alu_reg(code, BPF_LSH, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_shl\n");
			break;

		case DW_OP_shr:
		case DW_OP_shra:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (alu_reg(code, BPF_RSH, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_shr\n");
			break;

		case DW_OP_xor:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (alu_reg(code, BPF_XOR, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_4))
				return -1;

			pr_debug("OP_xor\n");
			break;

		case DW_OP_le:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (push_cmp(code, BPF_REG_5, BPF_JGE, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_5))
				return -1;

			pr_debug("OP_le\n");
			break;

		case DW_OP_ge:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (push_cmp(code, BPF_REG_5, BPF_JGE, BPF_REG_3, BPF_REG_4))
				return -1;

			if (push_reg(code, BPF_REG_5))
				return -1;

			pr_debug("OP_ge\n");
			break;

		case DW_OP_eq:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (push_cmp(code, BPF_REG_5, BPF_JEQ, BPF_REG_3, BPF_REG_4))
				return -1;

			if (push_reg(code, BPF_REG_5))
				return -1;

			pr_debug("OP_eq\n");
			break;

		case DW_OP_lt:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (push_cmp(code, BPF_REG_5, BPF_JGT, BPF_REG_4, BPF_REG_3))
				return -1;

			if (push_reg(code, BPF_REG_5))
				return -1;

			pr_debug("OP_lt\n");
			break;

		case DW_OP_gt:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (push_cmp(code, BPF_REG_5, BPF_JGT, BPF_REG_3, BPF_REG_4))
				return -1;

			if (push_reg(code, BPF_REG_5))
				return -1;

			pr_debug("OP_gt\n");
			break;

		case DW_OP_ne:
			if (pop_reg(code, BPF_REG_3))
				return -1;

			if (pop_reg(code, BPF_REG_4))
				return -1;

			if (push_cmp(code, BPF_REG_5, BPF_JNE, BPF_REG_3, BPF_REG_4))
				return -1;

			if (push_reg(code, BPF_REG_5))
				return -1;

			pr_debug("OP_ne\n");
			break;

		case DW_OP_skip:
			*addr += (int16_t) op1;

			pr_debug("OP_skip(%d)\n", (int16_t) op1);
			break;

		case DW_OP_bra:
			/* FUCK FUCK FUCK */
			if (pop_reg(code, BPF_REG_3))
				return -1;

			pr_err("officially FUCKed\n");

			pr_debug("OP_skip(%d)\n", (int16_t) op1);
			break;

		case DW_OP_nop:
			pr_debug("OP_nop\n");
			break;

		case DW_OP_call2:
		case DW_OP_call4:
		case DW_OP_call_ref:
		case DW_OP_fbreg:
		case DW_OP_piece:
		case DW_OP_push_object_address:
		case DW_OP_xderef:
		case DW_OP_xderef_size:
		default:
			pr_debug("Unexpected opcode 0x%x\n", opcode);
			return -EINVAL;

		} /* switch opcode */

		emit_debug(code);
	}

	if (pick_reg(code, BPF_REG_0, 0))
		return -1;

	emit_debug(code);
	return 0;
}

static struct unw_insn insn_entry[] = {
	{ .bi = BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_1, 0),                   .cstr = "MOV  R2, [R1]" },
	{ .bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, sizeof(unsigned long)),       .cstr = "ADD  R1, 8"    },
	{ .bi = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0),                   .cstr = "MOV  R1, [R1]" },
};

static struct unw_insn insn_exit[] = {
	{ .bi = BPF_EXIT_INSN(), .cstr = "RET" },
};

static int emit_entry(struct unw_code *code)
{
	return add_code(code, insn_entry, ARRAY_SIZE(insn_entry));
}

static int emit_exit(struct unw_code *code)
{
	return add_code(code, insn_exit, ARRAY_SIZE(insn_exit));
}

int emit_expr(struct unw_code *code, u8 *addr, unsigned long len)
{
	if (emit_entry(code))
		return -1;

	emit_debug(code);

	if (_emit_expr(code, addr, len))
		return -1;

	emit_debug(code);

	if (emit_exit(code))
		return -1;

	return 0;
}

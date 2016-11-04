#include <string.h>
#include <stdio.h>
#include <linux/dwarf_unwind.h>
#include <asm/errno.h>
#include "bpf.h"
#include "code.h"
#include "convert.h"
#include "read.h"

static int set_reg(struct unw_code *code, unsigned long reg, unsigned long val, unsigned long loc, int state_idx)
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
	insn[3].bi = BPF_ST_MEM(BPF_DW, BPF_REG_1, offset_loc, loc);

	return add_code(code, insn, 4);
}


static int restore(struct unw_code *code, unsigned long reg, int state_idx, int state_init)
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
	insn[2].bi = BPF_LDX_MEM(BPF_DW, BPF_REG_4, BPF_REG_1, offset_loc);
	insn[3].bi = BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_4, offset_init_loc);

	return add_code(code, insn, 4);
}

static int add_expr(struct unw_convert *c, u8 *addr, unsigned long len,
		    unsigned long reg, unsigned long loc)
{
	int idx = c->expr_cnt;
	struct unw_code *expr = c->expr + idx;
	struct unw_code *code = c->code;

	if (emit_expr(expr, addr, len))
		return -1;

	if (set_reg(code, reg, idx, loc, c->state_idx))
		return -1;

	c->expr_cnt++;
	return 0;
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

int emit_cfi(struct unw_convert *c, struct unw_frame *frame)
{
	struct unw_fde *fde = c->fde;
	struct unw_cie *cie = fde->cie;
	struct unw_code *code = c->code;
	u8 *addr     = frame->icode;
	u8 *addr_end = frame->icode + frame->ilen;
	u8 *curr_ip  = fde->loc_start;
	u8 *last_ip  = fde->loc_start;
	u8 *end_ip   = fde->loc_end;
	struct unw_insn insn[10];
	int state_init;
	char buf[100];

	state_init = c->state_idx;

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

			if (set_reg(code, operand, val, DU_LOCATION_MEMORY, c->state_idx))
				return -1;

			break;

		case DW_CFA_offset_extended:
		case DW_CFA_offset_extended_sf:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);
			val *= cie->align_data;

			if (set_reg(code, reg, val, DU_LOCATION_MEMORY, c->state_idx))
				return -1;

			break;

		case DW_CFA_restore:
			reg = operand;

			if (restore(code, reg, c->state_idx, state_init))
				return -1;

			break;

		case DW_CFA_restore_extended:
			reg = DU_READ_ULEB128(addr, addr_end);

			if (restore(code, reg, c->state_idx, state_init))
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

			if (set_reg(code, reg, 0, DU_LOCATION_UNDEF, c->state_idx))
				return -1;
			break;

		case DW_CFA_same_value:
			reg = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, reg, 0, DU_LOCATION_SAME, c->state_idx))
				return -1;
			break;

		case DW_CFA_register:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, reg, val, DU_LOCATION_REG, c->state_idx))
				return -1;
			break;

		case DW_CFA_remember_state:
			if ((c->state_idx + 1) >= DU_CFA_STACK_MAX) {
				return -EINVAL;
			}
			c->state_idx++;

			snprintf(buf, 100, "REG_1 += sizeof(struct du_state_regs)");
			insn[0].astr = strdup(buf);

			insn[0].bi = BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, sizeof(struct du_state_regs));

			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_restore_state:
			if (!c->state_idx) {
				return -EINVAL;
			}

			c->state_idx--;

			snprintf(buf, 100, "REG_1 -= sizeof(struct du_state_regs)");
			insn[0].astr = strdup(buf);

			insn[0].bi = BPF_ALU64_IMM(BPF_SUB, BPF_REG_1, sizeof(struct du_state_regs));

			if (add_code(code, insn, 1))
				return -1;
			break;

		case DW_CFA_def_cfa:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, DU_REG_CFA_REG_COLUMN, reg, DU_LOCATION_REG, c->state_idx))
				return -1;

			if (set_reg(code, DU_REG_CFA_OFF_COLUMN, val, DU_LOCATION_VALUE, c->state_idx))
				return -1;

			break;

		case DW_CFA_def_cfa_sf:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_SLEB128(addr, addr_end);
			val *= cie->align_data;

			if (set_reg(code, DU_REG_CFA_REG_COLUMN, reg, DU_LOCATION_REG, c->state_idx))
				return -1;

			if (set_reg(code, DU_REG_CFA_OFF_COLUMN, val, DU_LOCATION_VALUE, c->state_idx))
				return -1;

			break;

		case DW_CFA_def_cfa_register:
			reg = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, DU_REG_CFA_REG_COLUMN, reg, DU_LOCATION_REG, c->state_idx))
				return -1;

			break;

		case DW_CFA_def_cfa_offset:
			val = DU_READ_ULEB128(addr, addr_end);

			if (set_reg(code, DU_REG_CFA_OFF_COLUMN, val, DU_LOCATION_VALUE, c->state_idx))
				return -1;
			break;

		case DW_CFA_def_cfa_offset_sf:
			val = DU_READ_SLEB128(addr, addr_end);
			val *= cie->align_data;

			if (set_reg(code, DU_REG_CFA_OFF_COLUMN, val, DU_LOCATION_VALUE, c->state_idx))
				return -1;
			break;

		case DW_CFA_def_cfa_expression:
			len = DU_READ_ULEB128(addr, addr_end);

			if (add_expr(c, addr, len, DU_REG_CFA_REG_COLUMN, DU_LOCATION_EXPR))
				return -1;

			addr += len;

			break;

		case DW_CFA_expression:
			reg = DU_READ_ULEB128(addr, addr_end);
			len = DU_READ_ULEB128(addr, addr_end);

			if (add_expr(c, addr, len, reg, DU_LOCATION_EXPR))
				return -1;

			addr += len;
			break;

		case DW_CFA_val_expression:
			reg = DU_READ_ULEB128(addr, addr_end);
			len = DU_READ_ULEB128(addr, addr_end);

			if (add_expr(c, addr, len, reg, DU_LOCATION_EXPR_VALUE))
				return -1;

			addr += len;
			break;

		case DW_CFA_GNU_negative_offset_extended:
			reg = DU_READ_ULEB128(addr, addr_end);
			val = DU_READ_ULEB128(addr, addr_end);
			val *= -cie->align_data;

			if (set_reg(code, reg, val, DU_LOCATION_MEMORY, c->state_idx))
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

		emit_debug(code);

		if (curr_ip != last_ip) {
			if (emit_next(code))
				return -1;

			last_ip = curr_ip;
		}
	}

	return 0;
}


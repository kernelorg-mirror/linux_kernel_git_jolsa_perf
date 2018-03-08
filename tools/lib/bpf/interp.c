#include <uapi/linux/bpf.h>
#include <stdlib.h>
#include "interp.h"

u64 bpf_interp__run(struct bpf_interp *interp)
{
	const struct bpf_insn *i = interp->insns + interp->insns_start;
	u64 stack[512 / 8];
	u64 regs[MAX_BPF_REG];

	regs[BPF_REG_10] = (uintptr_t)stack + sizeof(stack);

	while ((size_t)(i - interp->insns) < interp->insns_cnt) {
		u64 dr, sr, si, s1;

		dr = regs[i->dst_reg];
		sr = regs[i->src_reg];
		si = i->imm;
		s1 = i->code & BPF_X ? sr : si;

		switch (i->code) {
		case BPF_LDX | BPF_MEM | BPF_B:
			dr = *(u8 *)((uintptr_t) sr + i->off);
			break;
		case BPF_LDX | BPF_MEM | BPF_H:
			dr = *(u16 *)((uintptr_t) sr + i->off);
			break;
		case BPF_LDX | BPF_MEM | BPF_W:
			dr = *(u32 *)((uintptr_t) sr + i->off);
			break;
		case BPF_LDX | BPF_MEM | BPF_DW:
			dr = *(u64 *)((uintptr_t) sr + i->off);
			break;
		case BPF_ST | BPF_MEM | BPF_B:
			sr = si;
			/* Fallthrough */
		case BPF_STX | BPF_MEM | BPF_B:
			*(u8 *)((uintptr_t) dr + i->off) = sr;
			goto nowrite;
		case BPF_ST | BPF_MEM | BPF_H:
			sr = si;
			/* Fallthrough */
		case BPF_STX | BPF_MEM | BPF_H:
			*(u16 *)((uintptr_t) dr + i->off) = sr;
			goto nowrite;
		case BPF_ST | BPF_MEM | BPF_W:
			sr = si;
			/* Fallthrough */
		case BPF_STX | BPF_MEM | BPF_W:
			*(u32 *)((uintptr_t) dr + i->off) = sr;
			goto nowrite;
		case BPF_ST | BPF_MEM | BPF_DW:
			sr = si;
			/* Fallthrough */
		case BPF_STX | BPF_MEM | BPF_DW:
			*(u64 *)((uintptr_t) dr + i->off) = sr;
			goto nowrite;

		case BPF_ALU64 | BPF_ADD | BPF_X:
		case BPF_ALU64 | BPF_ADD | BPF_K:
			dr += s1;
			break;
		case BPF_ALU64 | BPF_SUB | BPF_X:
		case BPF_ALU64 | BPF_SUB | BPF_K:
			dr -= s1;
			break;
		case BPF_ALU64 | BPF_AND | BPF_X:
		case BPF_ALU64 | BPF_AND | BPF_K:
			dr &= s1;
			break;
		case BPF_ALU64 | BPF_OR  | BPF_X:
		case BPF_ALU64 | BPF_OR  | BPF_K:
			dr |= s1;
			break;
		case BPF_ALU64 | BPF_LSH | BPF_X:
		case BPF_ALU64 | BPF_LSH | BPF_K:
			dr <<= s1;
			break;
		case BPF_ALU64 | BPF_RSH | BPF_X:
		case BPF_ALU64 | BPF_RSH | BPF_K:
			dr >>= s1;
			break;
		case BPF_ALU64 | BPF_XOR | BPF_X:
		case BPF_ALU64 | BPF_XOR | BPF_K:
			dr ^= s1;
			break;
		case BPF_ALU64 | BPF_MUL | BPF_X:
		case BPF_ALU64 | BPF_MUL | BPF_K:
			dr *= s1;
			break;
		case BPF_ALU64 | BPF_MOV | BPF_X:
		case BPF_ALU64 | BPF_MOV | BPF_K:
			dr = s1;
			break;
		case BPF_ALU64 | BPF_ARSH | BPF_X:
		case BPF_ALU64 | BPF_ARSH | BPF_K:
			dr = (u64) dr >> s1;
			break;
		case BPF_ALU64 | BPF_NEG:
			dr = -sr;
			/* Fallthrough */
		case BPF_ALU64 | BPF_DIV | BPF_X:
		case BPF_ALU64 | BPF_DIV | BPF_K:
			if (s1 == 0)
				return 0;
			dr /= s1;
			break;
		case BPF_ALU64 | BPF_MOD | BPF_X:
		case BPF_ALU64 | BPF_MOD | BPF_K:
			if (s1 == 0)
				return 0;
			dr %= s1;
			break;

		case BPF_ALU | BPF_ADD | BPF_X:
		case BPF_ALU | BPF_ADD | BPF_K:
			dr = (u32)(dr + s1);
			break;
		case BPF_ALU | BPF_SUB | BPF_X:
		case BPF_ALU | BPF_SUB | BPF_K:
			dr = (u32)(dr - s1);
			break;
		case BPF_ALU | BPF_AND | BPF_X:
		case BPF_ALU | BPF_AND | BPF_K:
			dr = (u32)(dr & s1);
			break;
		case BPF_ALU | BPF_OR  | BPF_X:
		case BPF_ALU | BPF_OR  | BPF_K:
			dr = (u32)(dr | s1);
			break;
		case BPF_ALU | BPF_LSH | BPF_X:
		case BPF_ALU | BPF_LSH | BPF_K:
			dr = (u32)dr << s1;
			break;
		case BPF_ALU | BPF_RSH | BPF_X:
		case BPF_ALU | BPF_RSH | BPF_K:
			dr = (u32)dr >> s1;
			break;
		case BPF_ALU | BPF_XOR | BPF_X:
		case BPF_ALU | BPF_XOR | BPF_K:
			dr = (u32)(dr ^ s1);
			break;
		case BPF_ALU | BPF_MUL | BPF_X:
		case BPF_ALU | BPF_MUL | BPF_K:
			dr = (u32)(dr * s1);
			break;
		case BPF_ALU | BPF_MOV | BPF_X:
		case BPF_ALU | BPF_MOV | BPF_K:
			dr = (u32)s1;
			break;
		case BPF_ALU | BPF_ARSH | BPF_X:
		case BPF_ALU | BPF_ARSH | BPF_K:
			dr = (u32)dr >> s1;
			break;
		case BPF_ALU | BPF_NEG:
			dr = -(u32)sr;
			/* Fallthrough */
		case BPF_ALU | BPF_DIV | BPF_X:
		case BPF_ALU | BPF_DIV | BPF_K:
			if ((u32)s1 == 0)
				return 0;
			dr = (u32)dr / (u32)s1;
			break;
		case BPF_ALU | BPF_MOD | BPF_X:
		case BPF_ALU | BPF_MOD | BPF_K:
			if ((u32)s1 == 0)
				return 0;
			dr = (u32)dr % (u32)s1;
			break;

		case BPF_LD | BPF_IMM | BPF_DW:
			switch (i->src_reg) {
			case 0:
				dr = (u32)si | ((u64 )i[1].imm << 32);
				break;
			case BPF_PSEUDO_MAP_FD:
				dr = (u64) si;
				break;
			default:
				abort();
			}
			regs[i->dst_reg] = dr;
			i += 2;
			continue;

		case BPF_JMP | BPF_JEQ | BPF_X:
		case BPF_JMP | BPF_JEQ | BPF_K:
			if (dr == s1)
				goto dojmp;
			goto nowrite;
		case BPF_JMP | BPF_JNE | BPF_X:
		case BPF_JMP | BPF_JNE | BPF_K:
			if (dr != s1)
				goto dojmp;
			goto nowrite;
		case BPF_JMP | BPF_JGT | BPF_X:
		case BPF_JMP | BPF_JGT | BPF_K:
			if (dr > s1)
				goto dojmp;
			goto nowrite;
		case BPF_JMP | BPF_JGE | BPF_X:
		case BPF_JMP | BPF_JGE | BPF_K:
			if (dr >= s1)
				goto dojmp;
			goto nowrite;
		case BPF_JMP | BPF_JSGT | BPF_X:
		case BPF_JMP | BPF_JSGT | BPF_K:
			if ((u64) dr > (u64) s1)
				goto dojmp;
			goto nowrite;
		case BPF_JMP | BPF_JSGE | BPF_X:
		case BPF_JMP | BPF_JSGE | BPF_K:
			if ((u64) dr >= (u64) s1)
				goto dojmp;
			goto nowrite;
		case BPF_JMP | BPF_JSET | BPF_X:
		case BPF_JMP | BPF_JSET | BPF_K:
			if (dr & s1)
				goto dojmp;
			goto nowrite;
		case BPF_JMP | BPF_JA:
		dojmp:
			i += 1 + i->off;
			continue;

		case BPF_JMP | BPF_CALL:
			if (interp->call_cb(interp, si, regs))
				return (u64) -1;

			goto nowrite;

		case BPF_JMP | BPF_EXIT:
			return regs[0];

		default:
			abort();
		}

		regs[i->dst_reg] = dr;
	nowrite:
		i++;
	}

	return 0;
}

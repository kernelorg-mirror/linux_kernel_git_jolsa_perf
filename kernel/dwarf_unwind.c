#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/dwarf_unwind.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/ptrace.h>
#include <linux/uaccess.h>
#include <linux/perf_event.h>

#ifdef CONFIG_DWARF_UNWIND_DEBUG
# define pr(fmt, ...) printk(fmt, ##__VA_ARGS__)
#else
# define pr(fmt, ...)
#endif

extern const char __start___dunw_frame[], __stop___dunw_frame[];

static LIST_HEAD(modules_list);
static DEFINE_SPINLOCK(modules_lock);

struct unw_frame {
	struct rb_node	  rb_node;
	struct du_frame	 *frame;
	struct bpf_prog	 *prog[0];
};

struct unw_module {
	struct module		*mod;
	struct list_head	 list;

	struct rb_root		 frames;
};

struct unw_module core = {
	.mod	= NULL,
	.frames	= RB_ROOT,
};

BPF_CALL_5(bpf_unwind, void *, r1, void *, r2, void *, r3, void *, r4, void *, r5)
{
	pr("bpf_unwind R1 %p, R2 %p, R3 %p, R4 %p, R5 %p\n",
	   r1, r2, r3, r4, r5);
	return 0;
}

const struct bpf_func_proto bpf_unwind_proto = {
	.func		= bpf_unwind,
	.gpl_only	= false,
	.pkt_access	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
	.arg2_type      = ARG_ANYTHING,
	.arg3_type      = ARG_ANYTHING,
	.arg4_type      = ARG_ANYTHING,
	.arg5_type      = ARG_ANYTHING,
};

static const struct bpf_func_proto *
unwind_func_proto(enum bpf_func_id func_id)
{
	switch (func_id) {
	case BPF_FUNC_unwind:
		return &bpf_unwind_proto;
	default:
		return NULL;
	}
}

static const struct bpf_verifier_ops unwind_type_ops = {
	.get_func_proto		= unwind_func_proto,
};

static struct bpf_prog_type_list unwind_type __read_mostly = {
	.ops	= &unwind_type_ops,
	.type	= BPF_PROG_TYPE_UNWIND,
};

static struct unw_module *modules_find(struct module *mod)
{
	struct unw_module *m;

	list_for_each_entry(m, &modules_list, list) {
		if (m->mod == mod)
			return m;
	}

	return NULL;
}

static int in_frame(struct unw_frame *f, unsigned long ip)
{
	unsigned long start = (unsigned long) f->frame->loc_start;
	unsigned long end   = (unsigned long) f->frame->loc_end;

	if ((ip >= start) && (ip < end))
		return 0;

	return ip - start;
}

static int cmp_frame(struct unw_frame *a, struct unw_frame *b)
{
	return a->frame->loc_start - b->frame->loc_start;
}

static int add_frame(struct unw_frame *new, struct unw_module *mod)
{
	struct rb_root *root = &mod->frames;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*p != NULL) {
		struct unw_frame *frame;

		parent = *p;
		frame = rb_entry(parent, struct unw_frame, rb_node);

		if (cmp_frame(new, frame) < 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&new->rb_node, parent, p);
	rb_insert_color(&new->rb_node, root);
	return 0;
}

static struct unw_frame* find_frame_mod(struct unw_module *mod, unsigned long ip)
{
	struct rb_root *root = &mod->frames;
        struct rb_node **p = &root->rb_node;
        struct rb_node *parent = NULL;

        while (*p != NULL) {
                struct unw_frame *f;
                int ret;

                parent = *p;
                f = rb_entry(parent, struct unw_frame, rb_node);

                ret = in_frame(f, ip);
                if (ret < 0)
                        p = &(*p)->rb_left;
                else if (ret > 0)
                        p = &(*p)->rb_right;
                else
                        return f;
        }

        return NULL;
}

static __maybe_unused struct unw_frame* find_frame(unsigned long ip)
{
	return find_frame_mod(&core, ip);
}

static struct unw_frame *frame_alloc(struct du_frame *frame)
{
	size_t size;
	int cnt = 1;

	if (frame->expr)
		cnt += frame->expr->len;

	size = sizeof(struct unw_frame) + cnt * sizeof(struct bpf_prog *);
	return kzalloc(size, GFP_KERNEL);
}

static struct bpf_prog *frame_prog(struct bpf_insn *insn, __u32 len)
{
	struct bpf_prog *prog;
	int err;

	prog = bpf_prog_alloc(bpf_prog_size(len), 0);
	if (!prog)
		return NULL;

	memcpy(prog->insnsi, insn, len * sizeof(struct bpf_insn));

	prog->len  = len;
	prog->type = BPF_PROG_TYPE_UNWIND;
	prog->aux->ops = &unwind_type_ops;

	fixup_bpf_calls(prog);

	prog = bpf_prog_select_runtime(prog, &err);
	if (err < 0) {
		bpf_prog_free(prog);
		return NULL;
	}

	return prog;
}

static int frame_init(struct unw_frame *f)
{
	struct du_frame *frame = f->frame;
	struct bpf_prog *prog;

	prog = frame_prog(frame->insn, frame->len);
	if (!prog)
		return -ENOMEM;

	f->prog[0] = prog;

	if (frame->expr) {
		struct du_expr_array *arr = frame->expr;
		int i;

		for (i = 0; i < arr->len; i++) {
			prog = frame_prog(frame->insn, frame->len);
			if (!prog)
				return -ENOMEM;

			f->prog[i + 1] = prog;
		}
	}

	return 0;
}

static int __frames_add(struct unw_module *mod,
			struct du_frame **start,
			struct du_frame **stop)
{
	struct du_frame *frame, **p = start;
	struct unw_frame *new;

	while (p < stop) {
		int ret;

		frame = *p;

		new = frame_alloc(frame);
		if (!new)
			return -ENOMEM;

		new->frame = frame;

		ret = frame_init(new);
		if (ret)
			return ret;

		add_frame(new, mod);
		p++;
	}

	return 0;
}

static int frames_add(struct unw_module *m)
{
	struct du_frame **start, **stop;

	start = (struct du_frame **) __start___dunw_frame;
	stop  = (struct du_frame **) __stop___dunw_frame;

	return __frames_add(&core, start, stop);
}

static int module_add(struct module *mod)
{
	struct unw_module *m;
	unsigned long flags;
	int ret;

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	m->frames = RB_ROOT;
	m->mod    = mod;

	ret = frames_add(m);
	if (ret) {
		kfree(m);
		return ret;
	}

	spin_lock_irqsave(&modules_lock, flags);
	if (modules_find(mod))
		ret = -EINVAL;
	else
		list_add_tail(&m->list, &modules_list);
        spin_unlock_irqrestore(&modules_lock, flags);

	if (ret)
		kfree(m);

	return ret;
}

static u64 run_expr(struct unw_frame *f, struct du_regs *regs, u64 val, u64 idx)
{
	struct du_int_expr expr = {
		.val	= val,
		.regs	= regs,
	};

	return BPF_PROG_RUN(f->prog[idx + 1], (const void *) &expr);
}

static int __apply_state(struct unw_frame *f, struct du_regs *regs,
			 struct du_state_regs *state)
{
	struct du_state_reg *cfa_state;
	struct du_regs tmp_regs;
	unsigned long prev_ip;
	unsigned long prev_cfa;
	unsigned long cfa;
	int i;

	cfa_state = &state->reg[DU_REG_CFA_REG_COLUMN];

	prev_ip  = regs->reg[DU_REG_IP];
	prev_cfa = regs->reg[DU_REG_CFA];

	if (cfa_state->loc == DU_LOCATION_REG) {
		struct du_state_reg *sp_state;

		/*
		 * CFA is equal to [reg] + offset:
		 *
		 * As a special-case, if the stack-pointer is the CFA and the
		 * stack-pointer wasn't saved, popping the CFA implicitly pops
		 * the stack-pointer as well.
		 */

		sp_state = &state->reg[DU_REG_SP];

		if ((cfa_state->val == DU_REG_SP) &&
		    (sp_state->loc == DU_LOCATION_SAME))
			cfa = prev_cfa;
		else {
			unsigned long reg;

			reg = cfa_state->val;
			cfa = regs->reg[reg];
		}

		cfa += state->reg[DU_REG_CFA_OFF_COLUMN].val;

	} else {
		if ((cfa_state->loc != DU_LOCATION_EXPR) ||
		    (cfa_state->loc != DU_LOCATION_EXPR_VALUE))
			return -EINVAL;

		cfa = run_expr(f, regs, prev_cfa, cfa_state->val);

		if (cfa_state->loc == DU_LOCATION_EXPR)
			regs->reg[DU_REG_CFA] = *((unsigned long *) cfa);
		else
			regs->reg[DU_REG_CFA] = cfa;
	}

	regs->reg[DU_REG_CFA] = cfa;

	/* Suck new register values. */
	for (i = 0; i < DU_REGS_NUM; ++i) {
		if (state->reg[i].loc == DU_LOCATION_REG) {
			int reg = state->reg[i].val;
			tmp_regs.reg[i] = regs->reg[reg];
		}
	}

	/* And the rest. */
	for (i = 0; i < DU_REGS_NUM; ++i) {
		struct du_state_reg *rs = &state->reg[i];
		unsigned long *p;
		unsigned long val;

		switch (rs->loc) {
		case DU_LOCATION_REG:
			regs->reg[i] = tmp_regs.reg[i];
			break;

		case DU_LOCATION_SAME:
			break;

		case DU_LOCATION_MEMORY:
			p = (unsigned long *) (cfa + rs->val);

			if (probe_kernel_address(p, val))
				return -EFAULT;

			regs->reg[i] = val;
			break;

		case DU_LOCATION_EXPR:
		case DU_LOCATION_EXPR_VALUE:
			val = run_expr(f, regs, regs->reg[i], rs->val);

			if (rs->loc == DU_LOCATION_EXPR)
				regs->reg[i] = *((unsigned long *) val);
			else
				regs->reg[i] = val;

			break;

		case DU_LOCATION_UNDEF:
			regs->reg[i] = 0;

		case DU_LOCATION_VALUE:
			break;
		}
	}

	/* No change, too bad.. */
	if ((regs->reg[DU_REG_IP] == prev_ip) &&
	    (cfa == prev_cfa))
		return -EINVAL;

	return 0;
}

static int apply_state(struct unw_frame *f, struct du_state *state,
		       struct pt_regs *pregs, int idx)
{
	struct du_regs regs;
	struct du_state_regs *state_regs;
	int ret;

	du_arch_regs_get(&regs, pregs);

	state_regs = &state->stack[idx];

	ret = __apply_state(f, &regs, state_regs);
	if (!ret)
		du_arch_regs_set(&regs, pregs);

	return ret;
}

#define NR_CONTEXTS 4

static DEFINE_PER_CPU(int, recursion[NR_CONTEXTS]);

static inline int get_recursion_context(void)
{
	int *r = this_cpu_ptr(recursion);
	int rctx;

	if (in_nmi())
		rctx = 3;
	else if (in_irq())
		rctx = 2;
	else if (in_softirq())
		rctx = 1;
	else
		rctx = 0;

	if (r[rctx])
		return -1;

	r[rctx]++;
	barrier();

	return rctx;
}

static inline void put_recursion_context(int rctx)
{
	int *r = this_cpu_ptr(recursion);

	barrier();
	r[rctx]--;
}

static int unwind_step(struct du_unwind *u, struct pt_regs *regs)
{
	struct unw_frame *f;
	int ret;

	f = find_frame(regs->ip);
	if (!f)
		return -EINVAL;

	u->ip  = (unsigned long) f->frame->loc_start;
	u->end = regs->ip;

	ret = BPF_PROG_RUN(f->prog[0], (const void *) u);
	if (ret >= 0)
		ret = apply_state(f, &u->state, regs, ret);

	return ret;
}

static DEFINE_PER_CPU(struct du_unwind, du_ctx[NR_CONTEXTS]);

static int unwind_stack(struct pt_regs *regs, du_entry entry, void *data, int rctx)
{
	struct du_unwind *u, *ctx = this_cpu_ptr(du_ctx);
	struct pt_regs r;
	int ret = 0;

	u = &ctx[rctx];

	memset(u, 0, sizeof(*u));
	du_arch_state_init(&u->state.stack[0]);

	memcpy(&r, regs, sizeof(r));

	while (!ret && !unwind_step(u, &r)) {
		ret = entry(&r, data);
	}

	return ret;
}

int du_unwind_stack(struct pt_regs *regs, du_entry entry, void *data)
{
	int ret = 0, rctx;

	preempt_disable();

	rctx = get_recursion_context();
	if (rctx < 0)
		goto out;

	ret = unwind_stack(regs, entry, data, rctx);

	put_recursion_context(rctx);
out:
	preempt_enable();
	return ret;
}

static int entry_perf(struct pt_regs *regs, void *data)
{
	struct perf_callchain_entry_ctx *entry = data;

	return perf_callchain_store(entry, regs->ip);
}

void perf_du_dump_stack(struct perf_callchain_entry_ctx *entry, struct pt_regs *regs)
{
	if (perf_callchain_store(entry, regs->ip))
		return;

	du_unwind_stack(regs, entry_perf, entry);
}


static int entry_printk(struct pt_regs *regs, void *data __maybe_unused)
{
	printk("[%p] %pB\n", (void *) regs->ip, (void *) regs->ip);

	return 0;
}

static noinline __maybe_unused void du_dump_stack(void)
{
	struct pt_regs regs;

	regs_load(&regs);
	du_unwind_stack(&regs, entry_printk, NULL);
}

static ssize_t
test_write(struct file *filp, const char __user *ubuf,
	   size_t cnt, loff_t *ppos)
{
	printk("Testing dwarf unwind from process context.\n");

	du_dump_stack();
	return cnt;
}

static const struct file_operations test_fops = {
	.write = test_write,
};

static int __init unwind_init(void)
{
	bpf_register_prog_type(&unwind_type);

	if (!debugfs_create_file("unwind_test", 0644, NULL, NULL,
				 &test_fops))
		return -ENOMEM;

	return module_add(NULL);
}

module_init(unwind_init);

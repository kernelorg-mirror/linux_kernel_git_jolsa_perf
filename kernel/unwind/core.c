#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <uapi/linux/unwind.h>
#include <linux/ptrace.h>
#include "internal.h"

extern const char __start___unwind_frame[], __stop___unwind_frame[];
extern const char __start___unwind_data[],  __stop___unwind_data[];

static LIST_HEAD(modules_list);
static DEFINE_SPINLOCK(modules_lock);

static struct kmem_cache *kmem_frame;

struct unw_frame {
	struct rb_node		  rb_node;
	struct unwind_frame	 *frame;
	struct bpf_prog          *prog;
};

struct unw_module {
	struct module		*mod;
	struct list_head	 list;

	struct rb_root		 frames;
};

extern const char __start___unwind_data[], __stop___unwind_data[];

struct unw_module core = {
	.mod	= NULL,
	.frames	= RB_ROOT,
};

BPF_CALL_3(bpf_unwind, void *, p1, void *, p2, void *, p3)
{
	printk("bpf_unwind %p %p %p\n", p1, p2, p3);
	return 0;
}

const struct bpf_func_proto bpf_unwind_proto = {
	.func		= bpf_unwind,
	.gpl_only	= false,
	.pkt_access	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_ANYTHING,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
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

static struct unw_frame* find_frame(unsigned long ip)
{
	return find_frame_mod(&core, ip);
}

static int frame_init(struct unw_frame *f)
{
	struct unwind_frame *frame = f->frame;
	struct bpf_prog *prog;
	int err;

	prog = bpf_prog_alloc(bpf_prog_size(frame->len), 0);
	if (!prog)
		return -ENOMEM;

	prog->len  = frame->len;
	prog->type = BPF_PROG_TYPE_UNWIND;
	prog->aux->ops = &unwind_type_ops;

	memcpy(prog->insnsi, frame->insn, prog->len * sizeof(struct bpf_insn));

	f->prog = prog;

	fixup_bpf_calls(prog);

	prog = bpf_prog_select_runtime(prog, &err);
	if (err < 0) {
		bpf_prog_free(prog);
		return err;
	}

	return 0;
}

static int __frames_add(struct unw_module *mod,
			struct unwind_frame **start,
			struct unwind_frame **stop)
{
	struct unwind_frame *frame, **p = start;
	struct unw_frame *new;

	while (p < stop) {
		int ret;

		frame = *p;

		new = kmem_cache_alloc(kmem_frame, GFP_KERNEL);
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
	struct unwind_frame **start, **stop;

	start = (struct unwind_frame **) __start___unwind_frame;
	stop  = (struct unwind_frame **) __stop___unwind_frame;

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

static int apply_state(struct du_state *state, struct pt_regs *regs)
{
	return 0;
}

static int unwind_step(struct pt_regs *regs)
{
	static struct du_unwind u;
	struct unw_frame *f;
	int ret;

	f = find_frame(regs->ip);
	if (!f) {
		printk("error: failed to find frame\n");
		return -1;
	}

	memset(&u.state, 0, sizeof(u.state));
	u.ip  = regs->ip;
	u.end = (unsigned long) f->frame->loc_end;

	printk("KRAVA unwind_step ctx %p, ip 0x%lx, end %p\n", &u, u.ip, u.end);

	ret = BPF_PROG_RUN(f->prog, (const void *) &u);
	if (!ret)
		ret = apply_state(&u.state, regs);

	return ret;
}

static void unwind_stack_regs(struct pt_regs *regs)
{
	struct pt_regs r;

	memcpy(&r, regs, sizeof(r));

	while (!unwind_step(&r)) {
		printk("[%p]\n", (void *) r.ip);
		break;
	}
}

static void unw_dump_stack(void)
{
	struct pt_regs regs;

	regs_load(&regs);
	unwind_stack_regs(&regs);
}

static ssize_t
test_write(struct file *filp, const char __user *ubuf,
	   size_t cnt, loff_t *ppos)
{
	printk("Testing dwarf unwind from process context.\n");

	unw_dump_stack();
	return cnt;
}

static const struct file_operations test_fops = {
	.write = test_write,
};

static int __init unwind_init(void)
{
	kmem_frame = KMEM_CACHE(unw_frame, SLAB_PANIC);
	bpf_register_prog_type(&unwind_type);

	if (!debugfs_create_file("unwind_test", 0644, NULL, NULL,
				 &test_fops))
		return -ENOMEM;

	return module_add(NULL);
}

module_init(unwind_init);

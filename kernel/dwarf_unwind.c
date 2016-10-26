#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/dwarf_unwind.h>
#include <linux/bpf.h>
#include <linux/filter.h>

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
	int cnt;

	cnt  = frame->expr ? frame->expr->len : 1;
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

static int __init unwind_init(void)
{
	return module_add(NULL);
}

module_init(unwind_init);

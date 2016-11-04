#include <linux/types.h>
#include <stdlib.h>
#include <asm/errno.h>
#include "read.h"
#include "parse.h"

#define DU_EH_FRAME_CIE 0
#define DU_EXT_LO	0xfffffff0
#define DU_EXT_HI	0xffffffff
#define DU_EXT_DWARF64	DU_EXT_HI

extern char __start_eh_frame[];
extern char __stop_eh_frame[];

unsigned long eh_frame_base;
unsigned long eh_frame_ptr;

struct parse_entry {
	u8 *start;
	u8 *end;
	int is64;
	union {
		u64 type;
		u8* cie_addr;
	};
	u8 *entries_start;
	u8 *entries_end;
};

static inline void *zalloc(size_t size)
{
        return calloc(1, size);
}

static struct unw_cie last_cie;
static struct rb_root cies;
static struct rb_root fdes;

static struct unw_cie* find_cie(u8 *addr)
{
	struct rb_node *n = cies.rb_node;

	while (n) {
		struct unw_cie *cie = rb_entry(n, struct unw_cie , frame.rb_node);

		if (addr < cie->addr)
			n = n->rb_left;
		else if (addr < cie->addr)
			n = n->rb_right;
		else
			return cie;
	}

	return NULL;
}

static int add_cie(struct unw_cie *entry)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct unw_cie *cie;

	p = &cies.rb_node;

	while (*p != NULL) {
		parent = *p;
		cie = rb_entry(parent, struct unw_cie, frame.rb_node);

		if (cie->addr < entry->addr)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	cie = zalloc(sizeof(*cie));
	if (!cie)
		return -ENOMEM;

	*cie = *entry;
	RB_CLEAR_NODE(&cie->frame.rb_node);

	rb_link_node(&cie->frame.rb_node, parent, p);
	rb_insert_color(&cie->frame.rb_node, &cies);
	return 0;
}

static int add_fde(struct unw_fde *entry)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct unw_fde *fde;

	p = &fdes.rb_node;

	while (*p != NULL) {
		parent = *p;
		fde = rb_entry(parent, struct unw_fde, frame.rb_node);

		if (fde->loc_start < entry->loc_start)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	fde = zalloc(sizeof(*fde));
	if (!fde)
		return -ENOMEM;

	*fde = *entry;
	RB_CLEAR_NODE(&fde->frame.rb_node);

	rb_link_node(&fde->frame.rb_node, parent, p);
	rb_insert_color(&fde->frame.rb_node, &fdes);
	return 0;
}


static int valid_align(struct unw_cie *cie)
{
	return ((cie->align_code < 255) &&
		(cie->align_data < 127) &&
		(cie->align_data > -127));
}

static int parse_entry_cie(u8 *p, struct parse_entry *entry)
{
	u8 ver, *end = entry->end;
	struct unw_cie  cie = {
		.addr = entry->start,
	};
	char *aug;

	ver = DU_READ(p, u8, end);
	aug = DU_READ_STR(p, end);

	cie.align_code = DU_READ_ULEB128(p, end);
	cie.align_data = DU_READ_SLEB128(p, end);

	if (!valid_align(&cie)) {
		fprintf(stderr, "failed: align_code %x, align_data %x\n",
		      cie.align_code, cie.align_data);
		return -EINVAL;
	}

	if (ver == 1)
		cie.ret_addr_column = DU_READ(p, u8, end);
	else
		cie.ret_addr_column = DU_READ_ULEB128(p, end);

	if (aug[0] == 'z') {
		u64 length;

		cie.aug_z = true;

		length = DU_READ_ULEB128(p, end);
		if ((p + length) >= end) {
			fprintf(stderr, "failed: 'z' len '0x%llx' over end (%p)\n",
			      length, p);
			return -EINVAL;
		}

		aug++;
        }

	while (*aug) {
		if (*aug == 'L') {
			p += 1;
			aug++;
		} else if (*aug == 'R') {
			cie.encoding = DU_READ(p, u8, end);
			aug++;
		} else {
			fprintf(stderr, "failed: unknow augmentation '%c'\n", *aug);
			return -EINVAL;
		}
	}

	cie.frame.icode = p;
	cie.frame.ilen  = end - p;

	fprintf(stderr, "CIE %p\n", cie.addr);
	last_cie = cie;

	return add_cie(&cie);
}

static int __parse_entry_fde(struct unw_cie *cie, u8 *p,
			     struct parse_entry *entry)
{
	struct unw_fde fde = {
		.cie = cie,
	};
	u8 *end = entry->end;
	unsigned long range;

	fde.loc_start = (u8 *) DU_READ_ENCODED_VALUE(p, end, cie->encoding);
	range = DU_READ_ENCODED_VALUE(p, end, cie->encoding & 0x0f);

	if (cie->aug_z)
		DU_READ_ULEB128(p, end);

	fde.frame.icode = p;
	fde.frame.ilen  = end - p;

	fde.loc_end = (u8 *) (fde.loc_start + range);

	fprintf(stderr, "FDE start %p, end %p\n", fde.loc_start, fde.loc_end);
	return add_fde(&fde);
}

static int parse_entry_fde(u8 *p, struct parse_entry *entry)
{
	struct unw_cie *cie = &last_cie;

	cie = find_cie(entry->cie_addr);
	if (!cie)
		return -EINVAL;

	return __parse_entry_fde(cie, p, entry);
}

static int parse_entry_len(u8 **p, struct parse_entry *entry)
{
	u8 *cur = *p;
	u8 *entry_end;
	u32 len32;
	u64 len;
	int is64;

	len32 = DU_READ(cur, u32, entry->entries_end);

	if (len32 >= DU_EXT_LO && len32 <= DU_EXT_HI) {
		if (len32 != DU_EXT_DWARF64)
			return -EINVAL;

		is64 = 1;
		len  = DU_READ(cur, u64, entry->entries_end);
        } else {
		is64 = 0;
		len  = len32;
	}

	entry_end = cur + len;
	if (entry_end > entry->entries_end)
		return -EINVAL;

	entry->end  = entry_end;
	entry->is64 = is64;

	*p = cur;
	return 0;
}

static int parse_entry_type(u8 **p, struct parse_entry *entry)
{
	u8 *cur = *p;
	u64 val;

	if (entry->is64)
		val = DU_READ(cur, u64, entry->end);
	else
		val = (u64) DU_READ(cur, u32, entry->end);

	if (val != DU_EH_FRAME_CIE) {
		u8 *cie_addr = cur - val - (entry->is64 ? 8 : 4);
		if (cie_addr < entry->entries_start)
			return -EINVAL;

		entry->cie_addr = cie_addr;
	} else
		entry->type = val;

	*p = cur;
	return 0;
}

static int parse_entry_is_cie(struct parse_entry *entry)
{
	return (entry->type == DU_EH_FRAME_CIE);
}

static int parse_entry(u8 *p, struct parse_entry *entry)
{
	if (parse_entry_len(&p , entry))
		return -EINVAL;

	if (parse_entry_type(&p, entry))
		return -EINVAL;

	if (parse_entry_is_cie(entry))
		return parse_entry_cie(p, entry);
	else
		return parse_entry_fde(p, entry);
}

int parse_fdes(u8 *start, u8 *end)
{
	u8 *pe = start;

	fprintf(stderr, "base %p\n", start);

	cies = RB_ROOT;
	fdes = RB_ROOT;

	while (pe < end) {
		struct parse_entry entry = {
			.start         = pe,
			.entries_start = start,
			.entries_end   = end,
		};
		int ret;

		ret = parse_entry(pe, &entry);
		if (ret)
			return ret;

		pe = entry.end;
	}

	return 0;
}

int walk_fdes(unw_fde_cb_t cb)
{
	struct rb_node *next;

	next = rb_first(&fdes);

	while (next) {
		struct unw_fde *fde = rb_entry(next, struct unw_fde, frame.rb_node);
		int ret;

		ret = cb(fde);
		if (ret)
			return ret;

		next = rb_next(next);
	}

	return 0;
}

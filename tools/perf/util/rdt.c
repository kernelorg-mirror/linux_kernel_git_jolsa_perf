#include <linux/compiler.h>
#include <errno.h>
#include "cpumap.h"
#include "util.h"
#include "rdt.h"

static int get_resource(char *str)
{
	static const char *name[RDT_NUM_RESOURCES] = {
		"L3", "L3DATA", "L3CODE", "L2",
	};
	int i;

	for (i = 0; i < RDT_NUM_RESOURCES; i++) {
		if (!strncmp(name[i], str, strlen(name[i])))
			return i;
	}

	return -1;
}

static int add_cpus(struct rdt_group *g, char *map)
{
	g->cpus = cpu_map__new(map);
	return g->cpus ? 0 : -1;
}

static int add_schemata(struct rdt_group *g, struct list_head *head)
{
	struct rdt_config *c, *c_ass;

	list_for_each_entry(c, head, list) {
		struct rdt_schemata *s;
		struct rdt_cbm cbm[100];
		int cnt = 0, idx;

		if (c->type != RDT_CONFIG_TYPE__SCHEMATA_LINE)
			return -1;

		idx = get_resource(c->schemata_line.name);
		if (idx < 0)
			return -1;

		s = &g->schemata[idx];
		s->enabled = true;

		list_for_each_entry(c_ass, c->schemata_line.head, list) {
			if (c_ass->type != RDT_CONFIG_TYPE__SCHEMATA_ASS)
				return -1;

			cbm[cnt].id  = strtoul(c_ass->schemata_ass.id, NULL, 10);
			cbm[cnt].val = strtoul(c_ass->schemata_ass.val, NULL, 16);
			cnt++;
		}

		s->cnt = cnt;
		s->cbm = memdup(cbm, cnt * sizeof(cbm[0]));
	}

	return 0;
}

int rdt_group__add(struct rdt_data *data __maybe_unused, char *name, struct list_head *head)
{
	struct rdt_config *c;
	struct rdt_group *g;

	g = zalloc(sizeof(*g));
	if (!g)
		return -ENOMEM;

	g->name = name;
	INIT_LIST_HEAD(&g->list);

	list_for_each_entry(c, head, list) {
		switch (c->type) {
		case RDT_CONFIG_TYPE__CPUS:
			if (add_cpus(g, c->cpus.map))
				return -1;
			break;
		case RDT_CONFIG_TYPE__SCHEMATA:
			if (add_schemata(g, c->schemata.head))
				return -1;
			break;
		default:
			return -1;
		};
	}

	list_add_tail(&g->list, &data->groups);
	return 0;
}

static int add_ids(struct rdt_resource *r __maybe_unused,
		   struct list_head *head __maybe_unused)
{
	return 0;
}

int rdt_resource__add(struct rdt_data *data, char *name, struct list_head *head)
{
	struct rdt_config *c;
	struct rdt_resource *r;
	int idx;

	idx = get_resource(name);
	if (idx < 0)
		return -1;

	r = &data->resource[idx];

	r->name    = name;
	r->enabled = true;

	list_for_each_entry(c, head, list) {
		switch (c->type) {
		case RDT_CONFIG_TYPE__CBM_MASK:
			r->cbm_mask = c->cbm_mask;
			break;
		case RDT_CONFIG_TYPE__MIN_CBM_BITS:
			r->min_cbm_bits = c->min_cbm_bits;
			break;
		case RDT_CONFIG_TYPE__NUM_CLOSIDS:
			r->num_closids = c->num_closids;
			break;
		case RDT_CONFIG_TYPE__IDS:
			if (add_ids(r, c->ids.head))
				return -1;
			break;
		default:
			return -1;
		};
	}

	return 0;
}

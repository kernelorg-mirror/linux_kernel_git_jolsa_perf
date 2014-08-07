#ifndef __PERF_POLLER
#define __PERF_POLLER

#include <poll.h>
#include <linux/list.h>

#define POLLER__HLIST_BITS 8
#define POLLER__HLIST_SIZE (1 << POLLER__HLIST_BITS)

struct poller_item;
struct poller;

typedef int (*poller_cb)(struct poller *p, struct poller_item *item);

struct poller_ops {
	poller_cb data;
	poller_cb error;
	poller_cb hup;
};

struct poller_item {
	int			 fd;
	struct hlist_node	 node;
};

struct poller {
	int			 n;
	int			 n_alloc;
	struct pollfd		*ptr;
	struct poller_ops	 ops;
	struct hlist_head	 items[POLLER__HLIST_SIZE];
};

void poller__init(struct poller *p);
void poller__cleanup(struct poller *p);
int poller__poll(struct poller *p, int timeout);
int poller__add(struct poller *p, struct poller_item *item);
void poller__del(struct poller *p, struct poller_item *item);
void poller__set_ops(struct poller *p, struct poller_ops *ops);
bool poller__empty(struct poller *p);
#endif /* __PERF_POLLER */

#ifndef __PERF_POLLER
#define __PERF_POLLER

#include <poll.h>
#include <linux/list.h>

#define POLLER__HLIST_BITS 8
#define POLLER__HLIST_SIZE (1 << POLLER__HLIST_BITS)

struct poller_item;

typedef int (*poller_cb)(struct poller_item *item);

struct poller_ops {
	poller_cb data;
	poller_cb error;
};

struct poller_item {
	int			 fd;
	void			*data;
	struct poller_ops	 ops;
	struct hlist_node	 node;
};

struct poller {
	int			 n;
	int			 n_alloc;
	struct pollfd		*ptr;
	struct hlist_head	 items[POLLER__HLIST_SIZE];
};

void poller_init(struct poller *p);
void poller_cleanup(struct poller *p);

int poller_poll(struct poller *p, int timeout);
int poller_poll_raw(struct poller *p, int timeout);

int poller_add(struct poller *p, struct poller_item *item);
int poller_del(struct poller *p, struct poller_item *item);

#endif /* __PERF_POLLER */

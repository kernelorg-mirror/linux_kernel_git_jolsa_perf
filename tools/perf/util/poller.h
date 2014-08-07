#ifndef __PERF_POLLER
#define __PERF_POLLER

/*
 * The interface functions are documented in the poller.c.
 *
 * Basically user has 'struct poller poller' object and
 * initialize it by calling:
 *
 *   poller__init(&poller);
 *
 * For each file descriptor define 'struct poller_item'
 * object like:
 *
 *   struct poller_item p = {
 *     .fd   = fd,              -> monitored fd
 *     .data = ...              -> user data
 *     .ops  = {                -> callbacks (optional)
 *       data   = data_cb,      -> data  callback called for POLLIN event
 *       error  = error_cb,     -> error callback called for POLLERR event
 *       hup    = hup_cb,       -> hup   callback called for POLLHUP event
 *     },
 *   }
 *
 *  Any defined 'struct poller_item' object could be then added
 *  (or removed) into poller object by:
 *
 *    poller__add(&poller, &p);
 *    poller__del(&poller, &p);
 *
 *  Each added object gets processed by poller__poll:
 *
 *    poller__poll(&poller, -1);
 *
 *  which calls poll(3) on installed (added) descriptors
 *  and calls callbacks for specific descriptors.
 *
 *  Note it's possible to call poller__add/del within
 *  the callback function.
 *
 *  If 'struct poller_item::ops' stays undefined, the
 *  'struct poller:ops' is checked and used instead.
 *
 *  To cleanup poller's object at the end call:
 *
 *     poller__cleanup(struct poller *p);
 */

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
	struct poller_ops	 ops;
	void			 *data;
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

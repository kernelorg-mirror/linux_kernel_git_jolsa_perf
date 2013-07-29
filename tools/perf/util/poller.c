
#include "types.h"
#include <linux/bitops.h>
#include <linux/hash.h>
#include <string.h>
#include <errno.h>
#include "perf.h"
#include "poller.h"
#include "asm/bug.h"

void poller_init(struct poller *p)
{
	int i;

	memset(p, 0x0, sizeof(*p));

	for (i = 0; i < POLLER__HLIST_SIZE ; ++i)
		INIT_HLIST_HEAD(&p->items[i]);
}

void poller_cleanup(struct poller *p)
{
	free(p->ptr);
}

static struct poller_item* item_find(struct poller *p, int fd)
{
	struct hlist_head *head;
	struct poller_item *item;
	int hash;

	hash = hash_64(fd, POLLER__HLIST_BITS);
	head = &p->items[hash];

	hlist_for_each_entry(item, head, node)
		if (item->fd == fd)
			return item;

	return NULL;
}

static int process(struct poller *p, int n)
{
	struct pollfd *pfd = p->ptr;
	int i;

	for (i = 0; (i < p->n) && n; pfd++,i++) {
		struct poller_item *item;
		int ret;

		if (!pfd->revents)
			continue;

		item = item_find(p, pfd->fd);

		if (pfd->revents & POLLIN)
			ret = item->ops.data(item);
		else
			ret = item->ops.error(item);

		if (ret)
			return ret;
	}

	return 0;
}

int poller_poll(struct poller *p, int timeout)
{
	int ret;

	ret = poll(p->ptr, p->n, timeout);
	if (ret < 0)
		return -1;

	if (ret)
		process(p, ret);

	return 0;
}

static struct pollfd *get_pfd(struct poller *p)
{
	struct pollfd *ptr = p->ptr;

	if (p->n + 1 < p->n_alloc)
		goto out;

	ptr = realloc(ptr, (p->n + 1) * sizeof (*ptr));
	if (!ptr)
		return NULL;

	p->n_alloc++;
	p->ptr = ptr;
 out:
	return ptr + p->n++;
}

static void put_pfd(struct poller *p, int fd)
{
	struct pollfd *pfd = p->ptr;
	int i;

	for (; i < p->n; pfd++, i++) {
		if (pfd->fd == fd)
			break;
	}

	if (WARN(i == p->n, "fd not found"))
		return;

	*pfd = *(p->ptr + p->n);
	p->n--;
}

int poller_add(struct poller *p, struct poller_item *item)
{
	struct pollfd *pfd;
	int hash;

	if (item_find(p, item->fd))
		return -EINVAL;

	pfd = get_pfd(p);
	if (!pfd)
		return -ENOMEM;

	pfd->fd     = item->fd;
	pfd->events = POLLIN;

	hash = hash_64(item->fd, POLLER__HLIST_BITS);
        hlist_add_head(&item->node, &p->items[hash]);
	return 0;
}

int poller_del(struct poller *p, struct poller_item *item)
{
	put_pfd(p, item->fd);
	hlist_del_init(&item->node);
	return 0;
}

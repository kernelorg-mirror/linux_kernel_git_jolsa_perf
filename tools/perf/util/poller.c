#include <linux/bitops.h>
#include <linux/hash.h>
#include <linux/string.h>
#include <asm/bug.h>
#include <string.h>
#include <errno.h>
#include "perf.h"
#include "poller.h"

void poller__init(struct poller *p)
{
	int i;

	memset(p, 0, sizeof(*p));
	for (i = 0; i < POLLER__HLIST_SIZE ; ++i)
		INIT_HLIST_HEAD(&p->items[i]);
}

void poller__cleanup(struct poller *p)
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

	hlist_for_each_entry(item, head, node) {
		if (item->fd == fd)
			return item;
	}

	return NULL;
}

static int process_item(struct poller *p, struct poller_item *item,
			short revents)
{
	struct poller_ops *ops = &p->ops;
	bool has_data  = !!ops->data;
	bool has_error = !!ops->error;
	bool has_hup   = !!ops->hup;

	if (revents & POLLIN)
		return has_data  ? ops->data(p, item) : 0;
	else if (revents & POLLERR)
		return has_error ? ops->error(p, item) : 0;
	else if (revents & POLLHUP)
		return has_hup   ? ops->hup(p, item) : 0;

	return 0;
}

static int process(struct poller *p, int nret)
{
	struct pollfd *pfd, *pfd_base;
	int n = p->n;
	int i, ret = 0;

	/*
	 * We allow to call poller__del from poll callbacks,
	 * we we need to duplicate pollfd table to be sure
	 * it's still there.
	 */
	pfd = pfd_base = memdup(p->ptr, p->n * sizeof(struct pollfd));
	if (!pfd)
		return -ENOMEM;

	for (i = 0; (i < n) && nret; pfd++, i++) {
		struct poller_item *item;

		if (!pfd->revents)
			continue;

		ret = -EINVAL;

		item = item_find(p, pfd->fd);
		if (WARN(!item, "poller: unknown fd found"))
			goto err;

		ret = process_item(p, item, pfd->revents);
		if (ret)
			goto err;

		nret--;
	}

err:
	free(pfd_base);
	return ret;
}

int poller__poll(struct poller *p, int timeout)
{
	int ret, err;

	ret = poll(p->ptr, p->n, timeout);
	if (ret < 0 || !ret)
		return ret;

	err = process(p, ret);
	return err ?: ret;
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

	for (i = 0; i < p->n; pfd++, i++) {
		if (pfd->fd == fd)
			break;
	}

	if (WARN(i == p->n, "poller: fd not found"))
		return;

	p->n--;
	*pfd = *(p->ptr + p->n);
}

int poller__add(struct poller *p, struct poller_item *item)
{
	struct pollfd *pfd;
	int hash;

	if (item_find(p, item->fd))
		return -EINVAL;

	pfd = get_pfd(p);
	if (!pfd)
		return -ENOMEM;

	pfd->fd     = item->fd;
	pfd->events = POLLIN|POLLERR|POLLHUP;

	hash = hash_64(item->fd, POLLER__HLIST_BITS);
	hlist_add_head(&item->node, &p->items[hash]);
	return 0;
}

void poller__del(struct poller *p, struct poller_item *item)
{
	put_pfd(p, item->fd);
	hlist_del_init(&item->node);
}

void poller__set_ops(struct poller *p, struct poller_ops *ops)
{
	p->ops = *ops;
}

bool poller__empty(struct poller *p)
{
	return p->n == 0;
}

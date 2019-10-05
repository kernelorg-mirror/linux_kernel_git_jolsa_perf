// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include "bpf_helpers.h"

char _license[] SEC("license") = "GPL";
struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} perf_buf_map SEC(".maps");

#define _(P) (__builtin_preserve_access_index(P))

/* define few struct-s that bpf program needs to access */
struct callback_head {
	struct callback_head *next;
	void (*func)(struct callback_head *head);
};
struct dev_ifalias {
	struct callback_head rcuhead;
};

struct net_device /* same as kernel's struct net_device */ {
	int ifindex;
	volatile struct dev_ifalias *ifalias;
};

struct sk_buff {
	/* field names and sizes should match to those in the kernel */
        unsigned int            len,
                                data_len;
        __u16                   mac_len,
                                hdr_len;
        __u16                   queue_mapping;
	struct net_device *dev;
	/* order of the fields doesn't matter */
};

/* copy arguments from
 * include/trace/events/skb.h:
 * TRACE_EVENT(kfree_skb,
 *         TP_PROTO(struct sk_buff *skb, void *location),
 *
 * into struct below:
 */
struct trace_kfree_skb {
	struct sk_buff *skb;
	void *location;
};

SEC("raw_tracepoint/kfree_skb")
int trace_kfree_skb(struct trace_kfree_skb* ctx)
{
	struct sk_buff *skb = ctx->skb;
	struct net_device *dev;
	int ifindex;
	struct callback_head *ptr;
	void *func;
	__builtin_preserve_access_index(({
		dev = skb->dev;
		ifindex = dev->ifindex;
		ptr = dev->ifalias->rcuhead.next;
		func = ptr->func;
	}));

	bpf_printk("rcuhead.next %llx func %llx\n", ptr, func);
	bpf_printk("skb->len %d\n", _(skb->len));
	bpf_printk("skb->queue_mapping %d\n", _(skb->queue_mapping));
	bpf_printk("dev->ifindex %d\n", ifindex);

	/* send first 72 byte of the packet to user space */
	bpf_skb_output(skb, &perf_buf_map, (72ull << 32) | BPF_F_CURRENT_CPU,
		       &ifindex, sizeof(ifindex));
	return 0;
}

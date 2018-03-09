#ifndef __BPF_USERFUNCS_H
#define __BPF_USERFUNCS_H

#include <bpf-userapi.h>

static int (*bpfu_print)(const char *fmt, ...) =
	(void *) BPF_FUNC_USER_print;
static int (*bpfu_map_get_next_key)(void *map, void *key, void *value) =
	(void *) BPF_FUNC_USER_bpf_map_get_next_key;
static int (*bpfu_map_lookup_elem)(void *map, void *key, void *value) =
	(void *) BPF_FUNC_USER_bpf_map_lookup_elem;

#define print(fmt, ...)                                  \
({                                                       \
	char ____fmt[] = fmt;                            \
	bpfu_print(____fmt, ##__VA_ARGS__);              \
})

#endif /* __BPF_USERFUNCS_H */

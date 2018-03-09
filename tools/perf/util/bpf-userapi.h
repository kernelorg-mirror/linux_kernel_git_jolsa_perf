#ifndef __BPF_USERAPI_H
#define __BPF_USERAPI_H

enum {
	BPF_FUNC_USER_START			= 10000,
	BPF_FUNC_USER_print			= BPF_FUNC_USER_START,
	BPF_FUNC_USER_bpf_map_get_next_key,
	BPF_FUNC_USER_bpf_map_lookup_elem,
};

#endif /* __BPF_USERFUNCS_H */

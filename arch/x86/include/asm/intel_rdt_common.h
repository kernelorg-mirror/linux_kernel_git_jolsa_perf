#ifndef _ASM_X86_INTEL_RDT_COMMON_H
#define _ASM_X86_INTEL_RDT_COMMON_H

#define MSR_IA32_PQR_ASSOC	0x0c8f

/**
 * struct intel_pqr_rmid - RMID record for the PQR MSR
 * @rmid:		The cached Resource Monitoring ID
 * @rmid_usecnt:	The usage counter for rmid
 */
struct intel_pqr_rmid {
	u32			val;
	int			usecnt;
};

/**
 * struct intel_pqr_state - State cache for the PQR MSR
 * @rmid:		The cached Resource Monitoring ID record
 * @closid:		The cached Class Of Service ID
 *
 * The upper 32 bits of MSR_IA32_PQR_ASSOC contain closid and the
 * lower 10 bits rmid. The update to MSR_IA32_PQR_ASSOC always
 * contains both parts, so we need to cache them.
 *
 * The cache also helps to avoid pointless updates if the value does
 * not change.
 */
struct intel_pqr_state {
	struct intel_pqr_rmid	rmid;
	u32			closid;
};

DECLARE_PER_CPU(struct intel_pqr_state, pqr_state);

#endif /* _ASM_X86_INTEL_RDT_COMMON_H */

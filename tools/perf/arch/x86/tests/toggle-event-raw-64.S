
/*
 * XXX I'd normally do '#include <asm/unistd_64.h>', but it's
 * overloaded in ./util/include/asm/ with empty file. So using
 * my own syscall defines instead for now.
 */
#define __NR_openat 257
#define __NR_close 3

	.global test__toggle_event_raw_arch
test__toggle_event_raw_arch:
	movq $__NR_openat,%rax
	xorq %rdi, %rdi
	xorq %rdx, %rdx
	xorq %rcx, %rcx
	xorq %r8, %r8
	xorq %r9, %r9
	syscall

	nop			# 1
	nop			# 2

	movq $__NR_close,%rax	# 3
	xorq %rdi, %rdi		# 4
	syscall			# 5

	mov $5, %rax
	retq

#ifndef LENS_UTILS_H
#define LENS_UTILS_H

#include "libcflat.h"

#ifndef kr_info
#define kr_info(string, args...)                                               \
	do {                                                                   \
		printf(string, ##args);                                        \
	} while (0)
#endif

#define PC_VARS                                                                \
	unsigned int	c_store_start_hi, c_store_start_lo;                    \
	unsigned int	c_load_start_hi, c_load_start_lo;                      \
	unsigned int	c_load_end_hi, c_load_end_lo;                          \
	uint64_t	c_store_start;                                         \
	uint64_t	c_load_start;                                          \
	uint64_t	c_load_end;

#define PC_BEFORE_WRITE                                                        \
	asm volatile("rdtscp \n\t"                                             \
		     "lfence \n\t"                                             \
		     "mov %%edx, %[hi]\n\t"                                    \
		     "mov %%eax, %[lo]\n\t"                                    \
		     :                                                         \
		     [hi] "=r"(c_store_start_hi), [lo] "=r"(c_store_start_lo)  \
		     :                                                         \
		     : "rdx", "rax", "rcx");

#define PC_BEFORE_READ                                                         \
	asm volatile("rdtscp \n\t"                                             \
		     "lfence \n\t"                                             \
		     "mov %%edx, %[hi]\n\t"                                    \
		     "mov %%eax, %[lo]\n\t"                                    \
		     : [hi] "=r"(c_load_start_hi), [lo] "=r"(c_load_start_lo)  \
		     :                                                         \
		     : "rdx", "rax", "rcx");

#define PC_AFTER_READ                                                          \
	asm volatile("rdtscp \n\t"                                             \
		     "lfence \n\t"                                             \
		     "mov %%edx, %[hi]\n\t"                                    \
		     "mov %%eax, %[lo]\n\t"                                    \
		     : [hi] "=r"(c_load_end_hi), [lo] "=r"(c_load_end_lo)      \
		     :                                                         \
		     : "rdx", "rax", "rcx");                                   \

#define PC_PRINT_MEASUREMENT(meta)                                             \
	c_store_start =                                                        \
		(((uint64_t)c_store_start_hi) << 32) | c_store_start_lo;       \
	c_load_start = (((uint64_t)c_load_start_hi) << 32) | c_load_start_lo;  \
	c_load_end   = (((uint64_t)c_load_end_hi) << 32) | c_load_end_lo;      \
	kr_info("buf_addr %p\n", buf);                                         \
	kr_info("[%s] region_size %ld, block_size %ld, count %ld, "            \
		"cycle %ld - %ld - %ld, "        \
		"fence_strategy %s, fence_freq %s.\n",                         \
		meta.name, region_size, block_size, count, \
		c_store_start, c_load_start, c_load_end, meta.fence_strategy,  \
		meta.fence_freq);

#define PC_STRIDED_PRINT_MEASUREMENT(meta)                                     \
	c_store_start =                                                        \
		(((uint64_t)c_store_start_hi) << 32) | c_store_start_lo;       \
	c_load_start = (((uint64_t)c_load_start_hi) << 32) | c_load_start_lo;  \
	c_load_end   = (((uint64_t)c_load_end_hi) << 32) | c_load_end_lo;      \
	kr_info("buf_addr %p\n", buf);                                         \
	kr_info("[%s] region_size=%lu, block_size=%lu, region_skip=%lu, "   \
		"stride_size=%lu, count=%lu, " \
		"cycle=%ld:%ld:%ld, fence_strategy=%s, fence_freq=%s, "        \
		"repeat=%lu, op=%lu, region_align=%lu, "                    \
		"flush_after_load=%s, flush_l1=%s, record_timing=%s",          \
		meta.name, region_size, block_size, region_skip, strided_size, \
		count, c_store_start, c_load_start,        \
		c_load_end, meta.fence_strategy, meta.fence_freq, repeat, op,  \
		region_align, meta.flush_after_load, meta.flush_l1,            \
		meta.record_timing);

#define COVERT_PC_STRIDED_PRINT_MEASUREMENT(meta)                              \
	c_store_start =                                                        \
		(((uint64_t)c_store_start_hi) << 32) | c_store_start_lo;       \
	c_load_start = (((uint64_t)c_load_start_hi) << 32) | c_load_start_lo;  \
	c_load_end   = (((uint64_t)c_load_end_hi) << 32) | c_load_end_lo;      \
	kr_info("buf_addr %p\n", ci->buf);                                     \
	kr_info("[%s] region_size=%lu, block_size=%lu, region_skip=%lu, "   \
		"stride_size=%lu, count=%lu, " \
		"cycle=%ld:%ld:%ld, fence_strategy=%s, fence_freq=%s, "        \
		"repeat=%lu, op=%lu, region_align=%lu, "                     \
		"flush_after_load=%s, flush_l1=%s, record_timing=%s",          \
		meta.name, ci->region_size, ci->block_size, ci->region_skip,   \
		ci->strided_size, ci->count,           \
		c_store_start, c_load_start, c_load_end, meta.fence_strategy,  \
		meta.fence_freq, ci->repeat, ci->op, ci->region_align,         \
		meta.flush_after_load, meta.flush_l1, meta.record_timing);

#define PRINT_COVERT_INFO(func_name, ci)                                       \
	kr_info("buf_addr %p\n", ci->buf);                                     \
	kr_info("[%s] "                                                        \
		"role_type=%lu, "                                              \
		"total_data_bits=%lu, "                                       \
		"send_data=%p, "                                               \
		"buf=%p, "                                                     \
		"op=%lu, "                                                    \
		"region_size=%lu, "                                           \
		"region_skip=%lu, "                                           \
		"block_size=%lu, "                                            \
		"repeat=%lu, "                                                \
		"count=%lu, "                                                 \
		"chasing_func_index=%lu, "                                    \
		"strided_size=%lu, "                                          \
		"region_align=%lu, "                                          \
		"cindex=%lu, "                                                \
		"timing=%lu, "                                                \
		"delay=%lu, "                                                 \
		"delay_per_byte=%lu, ",                                       \
		func_name,                                                     \
		ci->role_type,                                                 \
		ci->total_data_bits,                                           \
		ci->send_data,                                                 \
		ci->buf,                                                       \
		ci->op,                                                        \
		ci->region_size,                                               \
		ci->region_skip,                                               \
		ci->block_size,                                                \
		ci->repeat,                                                    \
		ci->count,                                                     \
		ci->chasing_func_index,                                        \
		ci->strided_size,                                              \
		ci->region_align,                                              \
		ci->cindex,                                                    \
		ci->timing,                                                    \
		ci->delay,                                                     \
		ci->delay_per_byte);

#endif /* LENS_UTILS_H */

#ifndef LENS_UTILS_H
#define LENS_UTILS_H

#include "libcflat.h"

#ifndef kr_info
#define kr_info(string, args...)                                               \
	do {                                                                   \
		printf(string "\n", ##args);                                        \
	} while (0)
#endif

#define PC_VARS                                                                \
	unsigned int c_store_start_hi, c_store_start_lo;                       \
	unsigned int c_load_start_hi, c_load_start_lo;                         \
	unsigned int c_load_end_hi, c_load_end_lo;                             \
	uint64_t     c_store_start;                                            \
	uint64_t     c_load_start;                                             \
	uint64_t     c_load_end;

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
		     : "rdx", "rax", "rcx");

#define PC_PRINT_MEASUREMENT(meta)                                             \
	c_store_start =                                                        \
		(((uint64_t)c_store_start_hi) << 32) | c_store_start_lo;       \
	c_load_start = (((uint64_t)c_load_start_hi) << 32) | c_load_start_lo;  \
	c_load_end   = (((uint64_t)c_load_end_hi) << 32) | c_load_end_lo;      \
	kr_info("buf_addr %p\n", buf);                                         \
	kr_info("[%s] region_size %ld, block_size %ld, count %ld, "            \
		"cycle %ld - %ld - %ld, "                                      \
		"fence_strategy %s, fence_freq %s.\n",                         \
		meta.name, region_size, block_size, count, c_store_start,      \
		c_load_start, c_load_end, meta.fence_strategy,                 \
		meta.fence_freq);

#define PC_STRIDED_PRINT_MEASUREMENT(meta)                                     \
	c_store_start =                                                        \
		(((uint64_t)c_store_start_hi) << 32) | c_store_start_lo;       \
	c_load_start = (((uint64_t)c_load_start_hi) << 32) | c_load_start_lo;  \
	c_load_end   = (((uint64_t)c_load_end_hi) << 32) | c_load_end_lo;      \
	kr_info("buf_addr %p\n", buf);                                         \
	kr_info("[%s] region_size=%lu, block_size=%lu, region_skip=%lu, "      \
		"stride_size=%lu, count=%lu, "                                 \
		"cycle=%ld:%ld:%ld, fence_strategy=%s, fence_freq=%s, "        \
		"repeat=%lu, region_align=%lu, "                       \
		"flush_after_load=%s, flush_l1=%s, record_timing=%s",          \
		meta.name, region_size, block_size, region_skip, strided_size, \
		count, c_store_start, c_load_start, c_load_end,                \
		meta.fence_strategy, meta.fence_freq, repeat,              \
		region_align, meta.flush_after_load, meta.flush_l1,            \
		meta.record_timing);

#define COVERT_PC_STRIDED_PRINT_MEASUREMENT(meta)                              \
	c_store_start =                                                        \
		(((uint64_t)c_store_start_hi) << 32) | c_store_start_lo;       \
	c_load_start = (((uint64_t)c_load_start_hi) << 32) | c_load_start_lo;  \
	c_load_end   = (((uint64_t)c_load_end_hi) << 32) | c_load_end_lo;      \
	kr_info("buf_addr %p\n", ci->buf);                                     \
	kr_info("[%s] region_size=%lu, block_size=%lu, region_skip=%lu, "      \
		"stride_size=%lu, count=%lu, "                                 \
		"cycle=%ld:%ld:%ld, fence_strategy=%s, fence_freq=%s, "        \
		"repeat=%lu, region_align=%lu, "                               \
		"flush_after_load=%s, flush_l1=%s, record_timing=%s, "         \
		"total_cycle=%lu, cycle_beg_since_all_beg=%lu",                \
		meta.name, ci->region_size, ci->block_size, ci->region_skip,   \
		ci->strided_size, ci->count, c_store_start, c_load_start,      \
		c_load_end, meta.fence_strategy, meta.fence_freq, ci->repeat,  \
		ci->region_align, meta.flush_after_load,                       \
		meta.flush_l1, meta.record_timing, cycle_end - cycle_beg,      \
		cycle_beg - cycle_all_beg                                      \
		);

#define PRINT_CYCLES() \
	printf("Cycle stats:\n"); \
	printf("  [0] cycle_beg            : %10lu : %10lu\n", cycle_beg - cycle_all_beg, cycle_beg - cycle_all_beg); \
	printf("  [1] cycle_timing_init_beg: %10lu : %10lu\n", cycle_timing_init_beg - cycle_all_beg, cycle_timing_init_beg - cycle_beg); \
	printf("  [2] cycle_timing_init_end: %10lu : %10lu\n", cycle_timing_init_end - cycle_all_beg, cycle_timing_init_end - cycle_timing_init_beg); \
	printf("  [3] cycle_store_beg      : %10lu : %10lu\n", c_store_start - cycle_all_beg, c_store_start - cycle_timing_init_end); \
	printf("  [4] cycle_load_beg       : %10lu : %10lu\n", c_load_start - cycle_all_beg, c_load_start - c_store_start); \
	printf("  [5] cycle_load_end       : %10lu : %10lu\n", c_load_end - cycle_all_beg, c_load_end - c_load_start); \
	printf("  [6] cycle_end            : %10lu : %10lu\n", cycle_end - cycle_all_beg, cycle_end - c_load_end); \
	printf("  [7] cycle_ddl_end        : %10lu : %10lu\n", cycle_ddl_end - cycle_all_beg, cycle_ddl_end - cycle_end); \
	printf("  [8] cycle_stats_end      : %10lu : %10lu\n", cycle_stats_end - cycle_all_beg, cycle_stats_end - cycle_ddl_end);

#define PRINT_COVERT_INFO(ci)                                                  \
	kr_info("buf_addr %p\n", ci->buf);                                     \
	kr_info("role_type=%u, "                                               \
		"total_data_bits=%lu, "                                        \
		"send_data=%p, "                                               \
		"buf=%p, "                                                     \
		"region_size=%lu, "                                            \
		"region_skip=%lu, "                                            \
		"block_size=%lu, "                                             \
		"repeat=%lu, "                                                 \
		"count=%lu, "                                                  \
		"chasing_func_index=%lu, "                                     \
		"strided_size=%lu, "                                           \
		"region_align=%lu, "                                           \
		"cindex=%p, "                                                  \
		"timing=%p, "                                                  \
		"covert_file_id=%lu, "                                         \
		"iter_cycle_ddl=%lu, ",                                        \
		ci->role_type,                                                 \
		ci->total_data_bits,                                           \
		ci->send_data,                                                 \
		ci->buf,                                                       \
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
		ci->covert_file_id,                                            \
		ci->iter_cycle_ddl                                             \
		);                                                             \
	kr_info("\n");

#endif /* LENS_UTILS_H */

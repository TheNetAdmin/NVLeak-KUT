#ifndef LENS_MICROBENCH_COVERT_H
#define LENS_MICROBENCH_COVERT_H

#include "libcflat.h"
#include "print_info.h"

#define NVRAM_START (0x100000000)

typedef enum { sender, receiver, vanilla } covert_role_t;

typedef uint64_t cycle_t;

typedef struct {
	/* Data */
	// uint32_t  bit_id;
	// uint8_t	  bit_data;
	// uint64_t *bit_channel;
	uint64_t *timing;

	/* Cycles -- Conventional */
	cycle_t c_store_start;
	cycle_t c_load_start;
	cycle_t c_load_end;

	/* Cycles -- Profiling */
	cycle_t cycle_all_beg;
	cycle_t cycle_beg;
	cycle_t cycle_end;
	cycle_t cycle_timing_init_beg;
	cycle_t cycle_timing_init_end;
	cycle_t cycle_ddl_end;
	cycle_t cycle_stats_end;
} covert_result_t;

typedef struct {
	covert_role_t role_type;	/* Argument  */
	size_t	      covert_file_id;	/* Argument  */
	size_t	      total_data_bits;	/* Argument  */
	uint64_t *    send_data;	/* Hard code */
	char *	      buf;		/* Hard code */

	/* General info */
	uint64_t region_size;		/* Argument  */
	uint64_t region_skip;		/* Deduct    */
	uint64_t block_size;		/* Argument  */
	uint64_t repeat;		/* Argument  */
	uint64_t count;			/* Hard code */
	uint64_t iter_cycle_ddl;	/* Argument  */

	/* Pointer chasing info */
	size_t	  chasing_func_index;	/* Deduct    */
	uint64_t  strided_size;		/* Argument  */
	uint64_t  region_align;		/* Argument  */
	uint64_t *cindex;		/* Hard code */
	uint64_t *timing;		/* Hard code */

	/* Result */
	covert_result_t *result;	/* Generated */
} covert_info_t;


#define CURR_DATA(curr_data, curr_iter)                                        \
	curr_data = ((ci->send_data[curr_iter / 64] >> i) & 0x1);

void covert_ptr_chasing_load_only(covert_info_t *ci);
void covert_ptr_chasing_print(covert_info_t *ci);

#define GLOBAL_BIT	  36 /* 64GB/Global */
#define GLOBAL_WORKSET	  (1ULL << GLOBAL_BIT)
#define LATENCY_OPS_COUNT 1048576L

void vanilla_ptr_chasing(covert_info_t *ci);
#endif /* LENS_MICROBENCH_COVERT_H */

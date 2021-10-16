#ifndef LENS_MICROBENCH_COVERT_H
#define LENS_MICROBENCH_COVERT_H

#include "libcflat.h"

typedef enum { sender, receiver } covert_role_t;

typedef struct {
	covert_role_t role_type;
	size_t	      total_data_bits;
	uint64_t *    send_data;
	char *	      buf;
	uint64_t      op;

	/* General info */
	uint64_t region_size;
	uint64_t region_skip;
	uint64_t block_size;
	uint64_t repeat;
	uint64_t count;

	/* Pointer chasing info */
	size_t	  chasing_func_index;
	uint64_t  strided_size;
	uint64_t  region_align;
	uint64_t *cindex;
	uint64_t *timing;
} covert_info_t;

#ifndef kr_info
#define kr_info(string, args...)                                               \
	do {                                                                   \
		printf(string, ##args);                                        \
	} while (0)
#endif

#define CURR_DATA(curr_data, curr_iter)                                        \
	curr_data = ((ci->send_data[curr_iter / 64] >> i) & 0x1);

void covert_ptr_chasing_load_only(covert_info_t *ci);
#endif /* LENS_MICROBENCH_COVERT_H */

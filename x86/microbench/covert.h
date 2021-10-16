#ifndef LENS_MICROBENCH_COVERT_H
#define LENS_MICROBENCH_COVERT_H

#include "libcflat.h"

typedef enum { sender, receiver } covert_role_t;

typedef struct {
	covert_role_t role_type;	/* Argument  */
	size_t	      total_data_bits;	/* Argument  */
	uint64_t *    send_data;	/* Hard code */
	char *	      buf;		/* Hard code */

	/* General info */
	uint64_t region_size;		/* Argument  */
	uint64_t region_skip;		/* Deduct    */
	uint64_t block_size;		/* Argument  */
	uint64_t repeat;		/* Argument  */
	uint64_t count;			/* Hard code */

	/* Pointer chasing info */
	size_t	  chasing_func_index;	/* Deduct    */
	uint64_t  strided_size;		/* Argument  */
	uint64_t  region_align;		/* Argument  */
	uint64_t *cindex;		/* Hard code */
	uint64_t *timing;		/* Hard code */
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

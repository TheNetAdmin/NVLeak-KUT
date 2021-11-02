#include "covert.h"
#include "chasing.h"
#include "utils.h"
#include "libcflat.h"
#include "processor.h"
#include "print_info.h"

static inline uint64_t wait_until_ddl(uint64_t cycle_beg, uint64_t cycle_end,
				  uint64_t cycle_ddl)
{
	u32		   aux;
	unsigned long long cycle_cur;

	/* Target ddl */
	uint64_t cycle_tgt = cycle_beg + cycle_ddl;

	if (cycle_end > cycle_tgt) {
		debug_printf("WARNING: iter_cycle_end [%lu] exceeds iter_cycle_ddl [%lu], skipping wait\n",
		       cycle_end, cycle_tgt);
		return rdtscp(&aux);
	}

	do {
		/* 1024 cycle granularity */
		for (int i = 0; i < 1024 / 16; i++) {
			asm volatile("nop\n nop\n nop\n nop\n "
				     "nop\n nop\n nop\n nop\n "
				     "nop\n nop\n nop\n nop\n "
				     "nop\n nop\n nop\n nop\n ");
		}
		cycle_cur = rdtscp(&aux);
	} while (cycle_cur < cycle_tgt);

	return rdtscp(&aux);
}

void covert_ptr_chasing_load_only(covert_info_t *ci)
{
	PC_VARS;
	size_t	 i;
	size_t	 curr_data;
	uint64_t repeat = ci->repeat;

	char *	  covert_channel;
	/* KEEP IN SYNC WITH _print function */
	char *	  bit_0_channel = ci->buf;
	char *	  bit_1_channel = ci->buf + 4096;
	uint64_t *timing	= ci->timing;

	uint64_t cycle_all_beg;
	uint64_t cycle_beg, cycle_end;
	uint64_t cycle_timing_init_beg, cycle_timing_init_end;
	uint64_t cycle_ddl_end;
	uint64_t cycle_stats_end;
	uint32_t cycle_aux;

	/* Current result */
	covert_result_t *cr;

	cycle_all_beg = rdtscp(&cycle_aux);

	/* 
	 * NOTE: assume L2 16 way
	 *       - sender fill one set with 16 buffer blocks
	 *       - receiver fill same set with 16 other blocks
	 */
	// kr_info("covert_strategy=ptr_chasing_load_only\n");

	// kr_info("Init bit 0 channel: %p\n", bit_0_channel);
	chasing_func_list[ci->chasing_func_index].st_func(
		bit_0_channel, ci->region_size, ci->strided_size,
		ci->region_skip, ci->count, ci->repeat, ci->cindex, ci->timing);
	asm volatile("mfence \n" :::);

	// kr_info("Init bit 1 channel: %p\n", bit_1_channel);
	chasing_func_list[ci->chasing_func_index].st_func(
		bit_1_channel, ci->region_size, ci->strided_size,
		ci->region_skip, ci->count, ci->repeat, ci->cindex, ci->timing);
	asm volatile("mfence \n" :::);

	memset(ci->timing, 0, sizeof(uint64_t) * (ci->repeat * 4));

	switch (ci->role_type) {
	case 0: // sender
		// send 64 bits
		// kr_info("send_data_buffer=%p, total_data_bits=%lu\n",
		// 	ci->send_data, ci->total_data_bits);

		for (i = 0; i < ci->total_data_bits; i++) {
			cycle_beg = rdtscp(&cycle_aux);
			cr = &ci->result[i];
			timing = cr->timing;
			CURR_DATA(curr_data, i);
			// kr_info("Waiting to send bit_id=%ld, bit_data=%lu\n", i,
			// 	curr_data);
			cycle_timing_init_beg = rdtscp(&cycle_aux);
			TIMING_BUF_INIT(timing);
			cycle_timing_init_end = rdtscp(&cycle_aux);
			/* KEEP IN SYNC WITH _print function */
			if (curr_data == 0x0) {
				covert_channel = bit_0_channel;
			} else {
				covert_channel = bit_1_channel;
			}
			// kr_info("Send bit_data=%1lu, bit_id=%lu, channel=%p\n",
			// 	curr_data, i, covert_channel);

			PC_BEFORE_WRITE
			// chasing_func_list[ci->chasing_func_index].st_func(
			// 	covert_channel, ci->region_size,
			// 	ci->strided_size, ci->region_skip, ci->count,
			// 	ci->repeat, ci->cindex,
			// 	timing);
			PC_BEFORE_READ
			chasing_func_list[ci->chasing_func_index].ld_func(
				covert_channel, ci->region_size,
				ci->strided_size, ci->region_skip, ci->count,
				ci->repeat, ci->cindex,
				timing + repeat * 2);
			asm volatile("mfence \n" :::);
			PC_AFTER_READ

			cycle_end = rdtscp(&cycle_aux);

			cycle_ddl_end = wait_until_ddl(cycle_beg, cycle_end, ci->iter_cycle_ddl);

			// COVERT_PC_STRIDED_PRINT_MEASUREMENT(
			// 	chasing_func_list[ci->chasing_func_index]);
			// kr_info("[%s] ",
			// 	chasing_func_list[ci->chasing_func_index].name);
			// CHASING_PRINT_RECORD_TIMING("lat_st", (timing));
			// kr_info("[%s] ",
			// 	chasing_func_list[ci->chasing_func_index].name);
			// CHASING_PRINT_RECORD_TIMING("lat_ld",
			// 			    (timing + repeat * 2));
			// kr_info("\n");
			
			cycle_stats_end = rdtscp(&cycle_aux);

			RECORD_CYCLES(cr);

			// kr_info("\n");
		}
		break;
	case 1: // receiver
		/*
		 * NOTE: for now, receiver only monitors the first set, because
		 *       implementation is simpler, later may need to monitor
		 *       multiple sets
		 */
		for (i = 0; i < ci->total_data_bits; i++) {
			cycle_beg = rdtscp(&cycle_aux);
			cr = &ci->result[i];
			timing = cr->timing;
			// kr_info("Waiting to receive bit_id=%ld\n", i);
			
			cycle_timing_init_beg = rdtscp(&cycle_aux);
			TIMING_BUF_INIT(timing);
			cycle_timing_init_end = rdtscp(&cycle_aux);

			/* KEEP IN SYNC WITH _print function */
			covert_channel = bit_0_channel;

			// kr_info("Recv bit_id=%lu, channel=%p\n", i,
				// covert_channel);

			PC_BEFORE_WRITE
			// chasing_func_list[ci->chasing_func_index].st_func(
			// 	covert_channel, ci->region_size,
			// 	ci->strided_size, ci->region_skip, ci->count,
			// 	ci->repeat, ci->cindex,
			// 	timing);
			PC_BEFORE_READ
			chasing_func_list[ci->chasing_func_index].ld_func(
				covert_channel, ci->region_size,
				ci->strided_size, ci->region_skip, ci->count,
				ci->repeat, ci->cindex,
				timing + repeat * 2);
			asm volatile("mfence \n" :::);
			PC_AFTER_READ

			cycle_end = rdtscp(&cycle_aux);

			cycle_ddl_end = wait_until_ddl(cycle_beg, cycle_end, ci->iter_cycle_ddl);

			// COVERT_PC_STRIDED_PRINT_MEASUREMENT(
			// 	chasing_func_list[ci->chasing_func_index]);
			// kr_info("[%s] ",
			// 	chasing_func_list[ci->chasing_func_index].name);
			// CHASING_PRINT_RECORD_TIMING("lat_st", (timing));
			// kr_info("[%s] ",
			// 	chasing_func_list[ci->chasing_func_index].name);
			// CHASING_PRINT_RECORD_TIMING("lat_ld",
			// 			    (timing + repeat * 2));
			// kr_info("\n");
			
			cycle_stats_end = rdtscp(&cycle_aux);

			RECORD_CYCLES(cr);

			// kr_info("\n");
		}
		break;
	default:
		kr_info("UNKNOWN role_type = %d\n", ci->role_type);
		break;
	}

	return;
}

void covert_ptr_chasing_print(covert_info_t *ci)
{
	char *	  covert_channel;
	char *	  bit_0_channel = ci->buf;
	char *	  bit_1_channel = ci->buf + 4096;
	kr_info("covert_strategy=ptr_chasing_load_only\n");
	kr_info("Init bit 0 channel: %p\n", bit_0_channel);
	kr_info("Init bit 1 channel: %p\n", bit_1_channel);

	size_t	 i, ti;
	size_t	 curr_data;
	covert_result_t *cr;
	uint64_t *timing;
	uint64_t repeat = ci->repeat;

	switch (ci->role_type) {
	case 0: // sender
		// send 64 bits
		kr_info("send_data_buffer=%p, total_data_bits=%lu\n",
			ci->send_data, ci->total_data_bits);

		for (i = 0; i < ci->total_data_bits; i++) {
			cr = &ci->result[i];
			timing = cr->timing;
			CURR_DATA(curr_data, i);
			kr_info("Waiting to send bit_id=%ld, bit_data=%lu\n", i,
				curr_data);
			if (curr_data == 0x0) {
				covert_channel = bit_0_channel;
			} else {
				covert_channel = bit_1_channel;
			}
			kr_info("Send bit_data=%1lu, bit_id=%lu, channel=%p\n",
				curr_data, i, covert_channel);

			COVERT_PC_STRIDED_PRINT_MEASUREMENT_CR(
				chasing_func_list[ci->chasing_func_index], cr);
			kr_info("[%s] ",
				chasing_func_list[ci->chasing_func_index].name);
			CHASING_PRINT_RECORD_TIMING("lat_st", (timing));
			kr_info("[%s] ",
				chasing_func_list[ci->chasing_func_index].name);
			CHASING_PRINT_RECORD_TIMING("lat_ld",
						    (ci->timing + repeat * 2));
			kr_info("\n");
			
			PRINT_CYCLES(cr, ci);

			kr_info("\n");
		}
		break;
	case 1: // receiver
		for (i = 0; i < ci->total_data_bits; i++) {
			cr = &ci->result[i];
			timing = cr->timing;
			kr_info("Waiting to receive bit_id=%ld\n", i);

			covert_channel = bit_0_channel;
			kr_info("Recv bit_id=%lu, channel=%p\n", i,
				covert_channel);

			COVERT_PC_STRIDED_PRINT_MEASUREMENT_CR(
				chasing_func_list[ci->chasing_func_index], cr);
			kr_info("[%s] ",
				chasing_func_list[ci->chasing_func_index].name);
			CHASING_PRINT_RECORD_TIMING("lat_st", (timing));
			kr_info("[%s] ",
				chasing_func_list[ci->chasing_func_index].name);
			CHASING_PRINT_RECORD_TIMING("lat_ld",
						    (ci->timing + repeat * 2));
			kr_info("\n");
			
			PRINT_CYCLES(cr, ci);

			kr_info("\n");
		}
		break;
	default:
		kr_info("UNKNOWN role_type = %d\n", ci->role_type);
		break;
	}
}

void vanilla_ptr_chasing(covert_info_t *ci)
{
	unsigned int chasing_func_index;
	PC_VARS;

	size_t ti;
	uint64_t *timing	= ci->timing;
	uint64_t repeat = ci->repeat;
	
	uint64_t cycle_all_beg;
	uint64_t cycle_beg, cycle_end;
	uint32_t cycle_aux;

	cycle_all_beg = rdtscp(&cycle_aux);

	/* find pointer chasing benchmark */
	chasing_func_index = chasing_find_func(ci->block_size);
	if (chasing_func_index == -1) {
		kr_info("ERROR: Pointer chasing benchamrk with block size %ld byte not found\n",
			ci->block_size);
		goto vanilla_ptr_chasing_end;
	}

	/* decide region_skip */
	ci->region_skip = (ci->region_size / ci->block_size) * ci->strided_size;

	/* decide count */
	ci->count = 1;

	kr_info("Working set begin: %p end: %p, phy-begin: %lx, region_size=%lu, "
		"region_skip=%lu, block_size=%lu, strided_size=%lu, "
		"func=%s, count=%lu\n",
		ci->buf, ci->buf + GLOBAL_WORKSET, NVRAM_START, ci->region_size,
		ci->region_skip, ci->block_size, ci->strided_size,
		chasing_func_list[chasing_func_index].name, ci->count);
	
	cycle_beg = rdtscp(&cycle_aux);

	/* Init timing buffer */
	TIMING_BUF_INIT(timing);

	/* Pointer chasing read and write */

	PC_BEFORE_WRITE
	chasing_func_list[chasing_func_index].st_func(
		ci->buf, ci->region_size, ci->strided_size, ci->region_skip,
		ci->count, ci->repeat, ci->cindex, ci->timing);
	asm volatile("mfence \n" :::);
	PC_BEFORE_READ
	chasing_func_list[chasing_func_index].ld_func(
		ci->buf, ci->region_size, ci->strided_size, ci->region_skip,
		ci->count, ci->repeat, ci->cindex, ci->timing + repeat * 2);
	asm volatile("mfence \n" :::);
	PC_AFTER_READ

	cycle_end = rdtscp(&cycle_aux);

	COVERT_PC_STRIDED_PRINT_MEASUREMENT(
		chasing_func_list[chasing_func_index]);
	kr_info("[%s] ", chasing_func_list[chasing_func_index].name);
	CHASING_PRINT_RECORD_TIMING("lat_st", (timing));
	kr_info("[%s] ", chasing_func_list[chasing_func_index].name);
	CHASING_PRINT_RECORD_TIMING("lat_ld", (timing + repeat * 2));
	kr_info("\n");

vanilla_ptr_chasing_end:
	return;
}

#include "covert.h"
#include "chasing.h"
#include "utils.h"
#include "libcflat.h"

void covert_ptr_chasing_load_only(covert_info_t *ci)
{
	PC_VARS;
	size_t	 i;
	size_t	 curr_data;
	size_t	 ti;
	uint64_t repeat = ci->repeat;

	char *	  covert_channel;
	char *	  bit_0_channel = ci->buf;
	char *	  bit_1_channel = ci->buf + 4096;
	uint64_t *timing	= ci->timing;

	/* 
	 * NOTE: assume L2 16 way
	 *       - sender fill one set with 16 buffer blocks
	 *       - receiver fill same set with 16 other blocks
	 */
	kr_info("covert_strategy=ptr_chasing_load_only\n");


	/* Print macros */
	printf("CHASING_FENCE_STRATEGY_ID=%d\n", CHASING_FENCE_STRATEGY_ID);
	printf("CHASING_FENCE_FREQ_ID=%d\n", CHASING_FENCE_FREQ_ID);
	printf("CHASING_FLUSH_AFTER_LOAD=%d\n", CHASING_FLUSH_AFTER_LOAD);
	printf("CHASING_FLUSH_L1=%d\n", CHASING_FLUSH_L1);
	printf("CHASING_FLUSH_L1_TYPE=%s\n", CHASING_FLUSH_L1_TYPE);

	kr_info("Init bit 0 channel: %p\n", bit_0_channel);
	chasing_func_list[ci->chasing_func_index].st_func(
		bit_0_channel, ci->region_size, ci->strided_size,
		ci->region_skip, ci->count, ci->repeat, ci->cindex, ci->timing);
	asm volatile("mfence \n" :::);

	kr_info("Init bit 1 channel: %p\n", bit_1_channel);
	chasing_func_list[ci->chasing_func_index].st_func(
		bit_1_channel, ci->region_size, ci->strided_size,
		ci->region_skip, ci->count, ci->repeat, ci->cindex, ci->timing);
	asm volatile("mfence \n" :::);

	switch (ci->role_type) {
	case 0: // sender
		// send 64 bits
		kr_info("send_data_buffer=%p, total_data_bits=%lu\n",
			ci->send_data, ci->total_data_bits);

		for (i = 0; i < ci->total_data_bits; i++) {
			CURR_DATA(curr_data, i);
			kr_info("Waiting to send bit_id=%ld, bit_data=%lu\n", i,
				curr_data);
			TIMING_BUF_INIT(timing);
			if (curr_data == 0x0) {
				covert_channel = bit_0_channel;
			} else {
				covert_channel = bit_1_channel;
			}
			kr_info("Send bit_data=%1lu, bit_id=%lu, channel=%p\n",
				curr_data, i, covert_channel);

			PC_BEFORE_WRITE
			// chasing_func_list[ci->chasing_func_index].st_func(
			// 	covert_channel, ci->region_size,
			// 	ci->strided_size, ci->region_skip, ci->count,
			// 	ci->repeat, ci->cindex,
			// 	ci->timing);
			PC_BEFORE_READ
			chasing_func_list[ci->chasing_func_index].ld_func(
				covert_channel, ci->region_size,
				ci->strided_size, ci->region_skip, ci->count,
				ci->repeat, ci->cindex,
				ci->timing + repeat * 2);
			asm volatile("mfence \n" :::);
			PC_AFTER_READ

			COVERT_PC_STRIDED_PRINT_MEASUREMENT(
				chasing_func_list[ci->chasing_func_index]);
			kr_info("[%s] ",
				chasing_func_list[ci->chasing_func_index].name);
			CHASING_PRINT_RECORD_TIMING("lat_st", (timing));
			kr_info("[%s] ",
				chasing_func_list[ci->chasing_func_index].name);
			CHASING_PRINT_RECORD_TIMING("lat_ld",
						    (ci->timing + repeat * 2));
			kr_info("\n");
		}
		break;
	case 1: // receiver
		/*
		 * NOTE: for now, receiver only monitors the first set, because
		 *       implementation is simpler, later may need to monitor
		 *       multiple sets
		 */
		for (i = 0; i < ci->total_data_bits; i++) {
			kr_info("Waiting to receive bit_id=%ld\n", i);
			TIMING_BUF_INIT(timing);

			covert_channel = bit_0_channel;

			kr_info("Recv bit_id=%lu, channel=%p\n", i,
				covert_channel);

			PC_BEFORE_WRITE
			// chasing_func_list[ci->chasing_func_index].st_func(
			// 	covert_channel, ci->region_size,
			// 	ci->strided_size, ci->region_skip, ci->count,
			// 	ci->repeat, ci->cindex,
			// 	ci->timing);
			PC_BEFORE_READ
			chasing_func_list[ci->chasing_func_index].ld_func(
				covert_channel, ci->region_size,
				ci->strided_size, ci->region_skip, ci->count,
				ci->repeat, ci->cindex,
				ci->timing + repeat * 2);
			asm volatile("mfence \n" :::);
			PC_AFTER_READ

			COVERT_PC_STRIDED_PRINT_MEASUREMENT(
				chasing_func_list[ci->chasing_func_index]);
			kr_info("[%s] ",
				chasing_func_list[ci->chasing_func_index].name);
			CHASING_PRINT_RECORD_TIMING("lat_st", (timing));
			kr_info("[%s] ",
				chasing_func_list[ci->chasing_func_index].name);
			CHASING_PRINT_RECORD_TIMING("lat_ld",
						    (ci->timing + repeat * 2));
			kr_info("\n");
		}
		break;
	default:
		kr_info("UNKNOWN role_type = %d\n", ci->role_type);
		break;
	}

	return;
}

void vanilla_ptr_chasing(covert_info_t *ci)
{
	unsigned int chasing_func_index;
	PC_VARS;

	size_t ti;
	uint64_t *timing	= ci->timing;
	uint64_t repeat = ci->repeat;
	
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

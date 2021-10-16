#include "covert.h"
#include "chasing.h"
#include "utils.h"

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

	/* NOTE: assume L2 16 way
	 *       - sender fill one set with 16 buffer blocks
	 *       - receiver fill same set with 16 other blocks
	 */
	kr_info("covert_strategy=ptr_chasing_load_only");

	kr_info("Init bit 0 channel: %p", bit_0_channel);
	chasing_func_list[ci->chasing_func_index].st_func(
		bit_0_channel, ci->region_size, ci->strided_size,
		ci->region_skip, ci->count, ci->repeat, ci->cindex, ci->timing);
	asm volatile("mfence \n" :::);

	kr_info("Init bit 1 channel: %p", bit_1_channel);
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
		/* NOTE: for now, receiver only monitors the first set, because
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

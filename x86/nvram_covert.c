#include "libcflat.h"
#include "processor.h"
#include "alloc.h"
#include "microbench/chasing.h"
#include "microbench/covert.h"
#include "microbench/utils.h"
#include "microbench/print_info.h"

static phys_addr_t nvram_start = NVRAM_START;

static bool test_print_chasing_help(void)
{
	int len = sizeof(chasing_func_list) / sizeof(chasing_func_entry_t);

	printf("Total available pointer chasing benchmarks: %d\n", len);

	chasing_print_help();
	return true;
}

static covert_info_t ci;

static void print_usage(void)
{
	printf("Args: sender|receiver \n");
	printf("      covert_data_bits\n");
	printf("      region_size\n");
	printf("      block_size\n");
	printf("      stride_size\n");
	printf("      repeat\n");
	printf("      region_align\n");
	printf("      receiver_channel_page_offset\n");
	printf("      covert_file_id\n");
	printf("      iter_cycle_ddl\n");
}
static const unsigned total_args = 10;

enum { max_send_data_bytes = 8 * 1024 * 1024,
       buf_offset	   = 32 * 1024 * 1024,
       timing_offset	   = 16 * 1024 * 1024,
       cindex_offset	   = max_send_data_bytes,
};

static bool init_covert_info(int argc, char **argv)
{
	/* TODO: Change to use getenv instead of args */

	if (argc != total_args + 1) {
		printf("Wrong usage: expected %u args, but got %d\n",
		       total_args, argc - 1);
		print_usage();
		return false;
	}

	/* Arguments */
	if (strcmp(argv[1], "sender") == 0) {
		ci.role_type = sender;
	} else if (strcmp(argv[1], "receiver") == 0) {
		ci.role_type = receiver;
	} else if (strcmp(argv[1], "vanilla") == 0) {
		ci.role_type = vanilla;
	} else {
		print_usage();
		return false;
	}

	/* Developing */
	// if(ci.role_type == receiver) {
	// 	printf("DEVELOPING: adjust receiver buffer for 512MiB in case sender/receiver working on the same devdax\n");
	// 	nvram_start += 512 * 1024 * 1024;
	// }

	ci.total_data_bits = (size_t)atol(argv[2]);
	ci.region_size	   = (uint64_t)atol(argv[3]);
	ci.block_size	   = (uint64_t)atol(argv[4]);
	ci.strided_size	   = (uint64_t)atol(argv[5]);
	ci.repeat	   = (uint64_t)atol(argv[6]);
	ci.region_align	   = (uint64_t)atol(argv[7]);

	uint64_t receiver_channel_page_offset = (uint64_t)atol(argv[8]);

	ci.covert_file_id  = (size_t)atol(argv[9]);
	ci.iter_cycle_ddl  = (size_t)atol(argv[10]);

	/* Hard code */
	ci.send_data = (uint64_t *)nvram_start;
	ci.buf	     = (char *)(nvram_start + buf_offset);

	/* Allocated */
	ci.cindex    = (uint64_t *)malloc(sizeof(uint64_t) * (ci.region_size / ci.block_size));

	size_t timing_per_bit_size = sizeof(uint64_t) * (ci.repeat * 4);
	size_t timing_total_size = timing_per_bit_size * (ci.total_data_bits * 2);
	ci.timing		 = (uint64_t *)malloc(timing_total_size);
	memset(ci.timing, 0, timing_total_size);

	ci.result = (covert_result_t *)malloc(sizeof(covert_result_t) *
					      (ci.total_data_bits * 2));
	for (uint64_t i = 0; i < (ci.total_data_bits * 2); i++) {
		ci.result[i].timing = ci.timing + i * (ci.repeat * 4);
	}

	/* 
	 * NOTE: to ensure pc-stride always work on only one region, not to
	 *       move to the next region. This is for multi repeats.
	 */
	ci.count = 1;

	/* Other arguments */
	ci.region_skip	      = ci.region_size;
	ci.chasing_func_index = chasing_find_func(ci.block_size);

	/* Fix arguments */
	if (ci.role_type == receiver) {
		ci.total_data_bits *= 2;
		ci.buf += 4096 * receiver_channel_page_offset;
	}
	printf("receiver_channel_page_offset=%lu\n",
	       receiver_channel_page_offset);

	/* Check arguments */
	if (ci.role_type == sender || ci.role_type == receiver) {
		if (ci.total_data_bits * 8 > max_send_data_bytes) {
			printf("Error: total send data bits too large\n");
			return false;
		}
	}

	if (ci.chasing_func_index < 0) {
		printf("Error: chasing func not found: %ld\n",
		       ci.chasing_func_index);
		return false;
	}

	/* Print */
	covert_info_t *ci_ptr = &ci;
	PRINT_COVERT_INFO(ci_ptr);

	/* Print macros */
	printf("CHASING_FENCE_STRATEGY_ID=%d\n", CHASING_FENCE_STRATEGY_ID);
	printf("CHASING_FENCE_FREQ_ID=%d\n", CHASING_FENCE_FREQ_ID);
	printf("CHASING_FLUSH_AFTER_LOAD=%d\n", CHASING_FLUSH_AFTER_LOAD);
	printf("CHASING_FLUSH_L1=%d\n", CHASING_FLUSH_L1);
	printf("CHASING_FLUSH_L1_TYPE=%s\n", CHASING_FLUSH_L1_TYPE);

	return true;
}

static void print_covert_data(void)
{
	printf("Send data:\n");
	for (int i = 0; i < ci.total_data_bits / 64; i++) {
		printf("  [%04d]: 0x%016lx\n", i, ci.send_data[i]);
	}
}

static void covert_channel(void)
{
	if (ci.role_type == sender) {
		print_covert_data();
	}
	covert_ptr_chasing_load_only(&ci);
}

static bool check_and_set_up_sse(void)
{
	/* https://wiki.osdev.org/SSE */
	if (!this_cpu_has(X86_FEATURE_XMM) || !this_cpu_has(X86_FEATURE_XMM2)) {
		return false;
	}

	/* Clear CR0.EM, set CR0.MP */
	ulong cr0 = read_cr0();
	debug_printf("cr0 = 0x%016lx\n", cr0);
	cr0 &= ~(X86_CR0_EM);
	cr0 |= X86_CR0_MP;
	debug_printf("cr0 = 0x%016lx\n", cr0);
	write_cr0(cr0);

	/* Set CR4.OSFXSR and CR4.OSXMMEXCPT */
	ulong cr4 = read_cr4();
	debug_printf("cr4 = 0x%016lx\n", cr4);
	cr4 |= (1 << 9);
	cr4 |= (1 << 10);
	debug_printf("cr4 = 0x%016lx\n", cr4);
	write_cr4(cr4);

	/* Test SSE instruction, it will #UD if SSE is not properly set up */
	asm volatile("movq (0x0), %xmm0");

	return true;
}

int main(int argc, char **argv)
{
	/* Parse args */
	if (!init_covert_info(argc, argv)) {
		return 1;
	}

	/* Check and setup */
#ifdef NVRAM_COVERT_DEBUG_PRINT
	report(true, "NVRAM covert channel boot up.");
	report(this_cpu_has(X86_FEATURE_RDRAND), "CPU has rdrand feature");
	report(this_cpu_has(X86_FEATURE_RDTSCP), "CPU has rdtscp feature");
	report(test_print_chasing_help(),
	       "Print chasing microbenchmark help message");
	report(check_and_set_up_sse(), "Set up SSE extension");
#else
	check_and_set_up_sse();
#endif /* NVRAM_COVERT_DEBUG_PRINT */

	/* Init chasing index */
	init_chasing_index(ci.cindex, ci.region_size / ci.block_size);
	// printf("Chasing index:\n");
	// print_chasing_index(ci.cindex, ci.region_size / ci.block_size);

	if (ci.role_type == sender || ci.role_type == receiver) {
		/* Run covert channel */
		covert_channel();
	} else if (ci.role_type == vanilla) {
		/* Run vanilla pointer chasing */
		vanilla_ptr_chasing(&ci);
	}

	return report_summary();
}

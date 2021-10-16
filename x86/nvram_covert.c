#include "libcflat.h"
#include "processor.h"
#include "microbench/chasing.h"
#include "microbench/covert.h"
#include "microbench/utils.h"

static phys_addr_t nvram_start = 0x100000000;

static bool test_read_nvram(void)
{
	u64  data = 0;
	u64 *addr = (u64 *)nvram_start;
	data	  = addr[0];
	printf("Read data [0x%016lx] from addr [0x%016lx]\n", data,
	       (u64)(&addr[0]));

	if (data == 0xcccccccccccccccc)
		return true;
	else
		return false;
}

/* 
static bool test_write_nvram(void)
{
	u64 data = 0x1234432112344321;
	u64 *addr = (u64 *)nvram_start;

	addr[0] = data;
	asm volatile("clflush (%0)\n"
		     "mfence\n"
		     :
		     : "b"(&addr[0]));
	printf("Write data [0x%016lx] to addr [0x%016lx]\n", data, (u64)(&addr[0]));
	return true;
}
*/

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
}

enum { max_send_data_bytes = 8 * 1024 * 1024,
       buf_offset	   = 32 * 1024 * 1024,
       timing_offset	   = 16 * 1024 * 1024,
       cindex_offset	   = max_send_data_bytes,
};

static bool init_covert_info(int argc, char **argv)
{
	/* TODO: Change to use getenv instead of args */

	static const unsigned total_args = 7;

	if (argc < total_args + 1) {
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
	} else {
		print_usage();
		return false;
	}

	ci.total_data_bits = (size_t)atol(argv[2]);
	ci.region_size	   = (uint64_t)atol(argv[3]);
	ci.block_size	   = (uint64_t)atol(argv[4]);
	ci.strided_size	   = (uint64_t)atol(argv[5]);
	ci.repeat	   = (uint64_t)atol(argv[6]);
	ci.region_align	   = (uint64_t)atol(argv[7]);

	/* Hard code */
	ci.send_data = (uint64_t *)nvram_start;
	ci.buf	     = (char *)(nvram_start + buf_offset);
	ci.timing    = (uint64_t *)(nvram_start + timing_offset);
	ci.cindex    = (uint64_t *)(nvram_start + cindex_offset);
	/* 
	 * NOTE: to ensure pc-stride always work on only one region, not to
	 *       move to the next region. This is for multi repeats.
	 */
	ci.count = 1;

	/* Other arguments */
	ci.region_skip	      = ci.region_size;
	ci.chasing_func_index = chasing_find_func(ci.block_size);

	/* Check arguments */
	if (ci.total_data_bits * 8 > max_send_data_bytes) {
		printf("Error: total send data bits too large\n");
		return false;
	}

	if (ci.chasing_func_index < 0) {
		printf("Error: chasing func not found: %ld\n",
		       ci.chasing_func_index);
		return false;
	}

	/* Print */
	covert_info_t *ci_ptr = &ci;
	PRINT_COVERT_INFO(ci_ptr);

	return true;
}

static void covert_channel(void)
{
	init_chasing_index(ci.cindex, ci.region_size / ci.block_size);
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
	printf("cr0 = 0x%016lx\n", cr0);
	cr0 &= ~(X86_CR0_EM);
	cr0 |= X86_CR0_MP;
	printf("cr0 = 0x%016lx\n", cr0);
	write_cr0(cr0);

	/* Set CR4.OSFXSR and CR4.OSXMMEXCPT */
	ulong cr4 = read_cr4();
	printf("cr4 = 0x%016lx\n", cr4);
	cr4 |= (1 << 9);
	cr4 |= (1 << 10);
	printf("cr4 = 0x%016lx\n", cr4);
	write_cr4(cr4);
	
	/* Test SSE instruction, it will #UD if SSE is not properly set up */
	asm volatile ("movq (0x0), %xmm0");

	return true;
}

int main(int argc, char **argv)
{
	/* Parse args */
	if (!init_covert_info(argc, argv)) {
		return 1;
	}

	/* Check and setup */
	report(true, "NVRAM covert channel boot up.");
	report(test_read_nvram(), "Reading data from NVRAM");
	/* report(test_write_nvram(), "Writing data to NVRAM"); */
	report(this_cpu_has(X86_FEATURE_RDRAND), "CPU has rdrand feature");
	report(this_cpu_has(X86_FEATURE_RDTSCP), "CPU has rdtscp feature");
	report(test_print_chasing_help(),
	       "Print chasing microbenchmark help message");
	report(check_and_set_up_sse(), "Set up SSE extension");

	/* Run covert channel */
	covert_channel();

	return report_summary();
}

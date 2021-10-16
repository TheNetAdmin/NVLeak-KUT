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

	printf("TOtal available pointer chasing benchmarks: %d\n", len);

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
	if (argc < 7 + 1) {
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
}

int main(int argc, char **argv)
{
	if (!init_covert_info(argc, argv)) {
		return 1;
	}

	report(true, "NVRAM covert channel boot up.");
	report(test_read_nvram(), "Reading data from NVRAM");
	/* report(test_write_nvram(), "Writing data to NVRAM"); */
	report(this_cpu_has(X86_FEATURE_RDRAND), "CPU has rdrand feature");
	report(this_cpu_has(X86_FEATURE_RDTSCP), "CPU has rdtscp feature");
	report(test_print_chasing_help(),
	       "Print chasing microbenchmark help message");

	return report_summary();
}

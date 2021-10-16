#include "libcflat.h"
#include "processor.h"
#include "microbench/chasing.h"

static phys_addr_t nvram_start = 0x100000000;

static bool test_read_nvram(void)
{
	u64 data = 0;
	u64 *addr = (u64 *)nvram_start;
	data = addr[0];
	printf("Read data [0x%016lx] from addr [0x%016lx]\n", data, (u64)(&addr[0]));

	if (data == 0xcccccccccccccccc)
		return true;
	else
		return false;
}

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

static bool test_print_chasing_help(void)
{
	int len = sizeof(chasing_func_list) / sizeof(chasing_func_entry_t);
	
	printf("TOtal available pointer chasing benchmarks: %d\n", len);

	chasing_print_help();
	return true;
}

int main(int argc, char **argv)
{
	report(true, "NVRAM covert channel boot up.");
	report(test_read_nvram(), "Reading data from NVRAM");
	report(test_write_nvram(), "Writing data to NVRAM");
	report(this_cpu_has(X86_FEATURE_RDRAND), "CPU has rdrand feature");
	report(this_cpu_has(X86_FEATURE_RDTSCP), "CPU has rdtscp feature");
	report(test_print_chasing_help(), "Print chasing microbenchmark help message");
	return report_summary();
}

#include "libcflat.h"
#include "alloc_phys.h"

static void check_memory_size(void)
{
	phys_alloc_show();
}

int main(int argc, char **argv)
{
	report(true, "NVRAM covert channel boot up.");
	check_memory_size();
	return report_summary();
}

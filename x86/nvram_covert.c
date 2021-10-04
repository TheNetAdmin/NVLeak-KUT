#include "libcflat.h"

int main(int argc, char **argv)
{
	report(true, "NVRAM covert channel boot up.");
	return report_summary();
}

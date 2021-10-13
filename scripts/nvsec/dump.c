#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <x86intrin.h>

static void usage(void)
{
	printf("Usage: dump -f file_path [-d write_data]\n");
}

static void read_data_pmem(uint64_t *ptr, int count)
{
	printf("Reading data:\n");
	for (int i = 0; i < count; i++)
	{
		printf("    [%04u]: 0x%016lx\n", i, ptr[i]);
	}
}

static void write_data_pmem(uint64_t *ptr, uint64_t data)
{
	*ptr = data;
	_mm_clflush((void *)ptr);
	_mm_mfence();
}

int main(int argc, char *argv[])
{
	int opt;

	char *file_path;
	uint64_t write_data = 0;
	enum
	{
		READ,
		WRITE,
	} operation = READ;

	while ((opt = getopt(argc, argv, "f:d:")) != -1)
	{
		switch (opt)
		{
		case 'f':
			file_path = optarg;
			break;
		case 'd':
			write_data = strtoull(optarg, NULL, 0);
			operation = WRITE;
			break;
		default:
			usage();
			exit(1);
			break;
		}
	}

	if (file_path == NULL)
	{
		usage();
		exit(1);
	}

	printf("Args:\n");
	printf("    - file_path : %s\n", file_path);
	printf("    - opeeration: %u\n", operation);
	printf("    - write_data: 0x%016lx\n", write_data);

	printf("Opening file %s\n", file_path);
	int fd = open(file_path, O_RDWR);
	if (fd < 0)
	{
		printf("Could not open file: %s\n", file_path);
		exit(2);
	}

	struct stat statbuf;
	int err = fstat(fd, &statbuf);
	if (err < 0)
	{
		printf("Could not stat file: %s\n", file_path);
		exit(2);
	}

	size_t file_size = statbuf.st_size;
	if (file_size == 0)
	{
		printf("Warning: statbuf.st_size==0, force using file_size=1GiB\n");
		file_size = 1 * 1024 * 1024 * 1024;
	}

	uint64_t *ptr = MAP_FAILED;
	if (operation == READ)
	{
		ptr = (uint64_t *)mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
	}
	else
	{
		ptr = (uint64_t *)mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	}
	if (ptr == MAP_FAILED)
	{
		printf("Mapping failed\n");
		exit(2);
	}
	close(fd);

	read_data_pmem(ptr, 4);

	if (operation == WRITE)
	{
		printf("Writing data [0x%016lx] to [%p]\n", write_data, ptr);
		write_data_pmem(&(ptr[0]), write_data);
		read_data_pmem(ptr, 4);
	}

	err = munmap(ptr, file_size);
	if (err != 0)
	{
		printf("UnMapping Failed\n");
		exit(2);
	}

	return 0;
}

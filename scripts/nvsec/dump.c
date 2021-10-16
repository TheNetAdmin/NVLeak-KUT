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
	printf("Usage: dump -o out_file_path [-d write_data]\n");
	printf("Usage: dump -o out_file_path [-i in_file_path -s size_in_byte]\n");
}

static void read_data_pmem(uint64_t *ptr, int count)
{
	printf("Reading data:\n");
	for (int i = 0; i < count; i++) {
		printf("    [%04u]: 0x%016lx\n", i, ptr[i]);
	}
}

static void write_data_pmem(uint64_t *ptr, uint64_t data)
{
	*ptr = data;
	_mm_clflush((void *)ptr);
	_mm_mfence();
}

static void write_file_pmem(uint64_t *ptr, char *file_path, size_t file_size)
{
	int fd = open(file_path, O_RDONLY);
	if (fd < 0) {
		printf("Could not open file: %s\n", file_path);
		exit(2);
	}

	struct stat statbuf;
	int	    err = fstat(fd, &statbuf);
	if (err < 0) {
		printf("Could not stat file: %s\n", file_path);
		exit(2);
	}

	if (file_size > statbuf.st_size) {
		printf("File size mismatch, need %lu byte, but file only has %lu bytes\n",
		       file_size, statbuf.st_size);
		exit(2);
	}

	uint8_t *in_ptr = (uint8_t *)mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
	if (in_ptr == MAP_FAILED) {
		printf("Mapping failed\n");
		exit(2);
	}
	close(fd);

	uint8_t *out_ptr = (uint8_t *)ptr;

	for (size_t i = 0; i < file_size; i++) {
		out_ptr[i] = in_ptr[i];
		if (i % 64 == 63 || i == file_size - 1) {
			_mm_clflush((void *)out_ptr);
			_mm_mfence();
		}
	}

	err = munmap(in_ptr, file_size);
	if (err != 0) {
		printf("Unmapping Failed\n");
		exit(2);
	}
}

int main(int argc, char *argv[])
{
	int opt;

	char *	 out_file_path;
	char *	 in_file_path;
	size_t	 in_file_size;
	uint64_t write_data = 0;
	enum { READ,
	       WRITE_DATA,
	       WRITE_FILE,
	} operation = READ;

	while ((opt = getopt(argc, argv, "o:d:i:s:")) != -1) {
		switch (opt) {
		case 'o':
			out_file_path = optarg;
			break;
		case 'd':
			write_data = strtoull(optarg, NULL, 0);
			operation  = WRITE_DATA;
			break;
		case 'i':
			in_file_path = optarg;
			operation    = WRITE_FILE;
			break;
		case 's':
			in_file_size = strtoul(optarg, NULL, 0);
			break;
		default:
			usage();
			exit(1);
			break;
		}
	}

	if (out_file_path == NULL) {
		printf("ERROR: missing argument out_file_path\n");
		usage();
		exit(1);
	}

	printf("Args:\n");
	printf("    - out_file_path : %s\n", out_file_path);
	printf("    - opeeration: %u\n", operation);
	printf("    - write_data: 0x%016lx\n", write_data);

	printf("Opening file %s\n", out_file_path);
	int fd = open(out_file_path, O_RDWR);
	if (fd < 0) {
		printf("Could not open file: %s\n", out_file_path);
		exit(2);
	}

	struct stat statbuf;
	int	    err = fstat(fd, &statbuf);
	if (err < 0) {
		printf("Could not stat file: %s\n", out_file_path);
		exit(2);
	}

	size_t file_size = statbuf.st_size;
	if (file_size == 0) {
		printf("Warning: statbuf.st_size==0, force using file_size=1GiB\n");
		file_size = 1 * 1024 * 1024 * 1024;
	}

	uint64_t *ptr = MAP_FAILED;
	if (operation == READ) {
		ptr = (uint64_t *)mmap(NULL, file_size, PROT_READ, MAP_SHARED,
				       fd, 0);
	} else {
		ptr = (uint64_t *)mmap(NULL, file_size, PROT_READ | PROT_WRITE,
				       MAP_SHARED, fd, 0);
	}
	if (ptr == MAP_FAILED) {
		printf("Mapping failed\n");
		exit(2);
	}
	close(fd);

	read_data_pmem(ptr, 4);

	if (operation == WRITE_DATA) {
		printf("Writing data [0x%016lx] to [%p]\n", write_data, ptr);
		write_data_pmem(&(ptr[0]), write_data);
		read_data_pmem(ptr, 4);
	} else if (operation == WRITE_FILE) {
		printf("Writing file [%s] (size [%lu]) to [%p]\n", in_file_path,
		       in_file_size);
		write_file_pmem(ptr, in_file_path, in_file_size);
		read_data_pmem(ptr, (in_file_size / 8) + 1);
	}

	err = munmap(ptr, file_size);
	if (err != 0) {
		printf("Unmapping Failed\n");
		exit(2);
	}

	return 0;
}

// SPDX-License-Identifier: MIT

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "../module/pseud_defs.h"

#define STR_START "hello, world!"
#define STR_END "goodbye, world!"

int main(void)
{
    char *addr;
    int fd;
    char *wr_ptr;
    off_t offset = 0;
    off_t wr_start = 0;
    off_t wr_end = DEVMEM_LEN - sizeof(STR_END);

    fd = open("/dev/pseud_1", O_RDWR);

    if (fd == -1) {
        perror("open device file");
        exit(EXIT_FAILURE);
    }

    addr =
        mmap(NULL, DEVMEM_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);

    if (addr == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }

    wr_ptr = addr + wr_start;
    memcpy(wr_ptr, STR_START, sizeof(STR_START));

    wr_ptr = addr + wr_end;
    memcpy(wr_ptr, STR_END, sizeof(STR_END));

    printf("%s\n", addr + wr_start);
    printf("%s\n", addr + wr_end);

    munmap(addr, DEVMEM_LEN);
    close(fd);

    exit(EXIT_SUCCESS);
}

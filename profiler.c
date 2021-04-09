#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/mman.h>
#include <sys/stat.h>

void* target;
uint32_t* timing_data;


static __inline__ uint64_t timer_start (void) {
        unsigned cycles_low, cycles_high;
        asm volatile ("CPUID\n\t"
                    "RDTSC\n\t"
                    "mov %%edx, %0\n\t"
                    "mov %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
                    "%rax", "%rbx", "%rcx", "%rdx");
        return ((uint64_t)cycles_high << 32) | cycles_low;
}

static __inline__ uint64_t timer_stop (void) {
        unsigned cycles_low, cycles_high;
        asm volatile("RDTSCP\n\t"
                    "mov %%edx, %0\n\t"
                    "mov %%eax, %1\n\t"
                    "CPUID\n\t": "=r" (cycles_high), "=r" (cycles_low):: "%rax",
                    "%rbx", "%rcx", "%rdx");
        return ((uint64_t)cycles_high << 32) | cycles_low;
}

static __inline__ void maccess(void *p) {
  asm volatile("movq (%0), %%rax\n" : : "c"(p) : "rax");
}

void *map_offset(const char *file, size_t offset) {
  int fd = open(file, O_RDONLY);
  if (fd < 0)
  {
    return NULL;

  }

  char *mapaddress = mmap(0, sysconf(_SC_PAGE_SIZE), PROT_READ, MAP_PRIVATE, fd, offset & ~(sysconf(_SC_PAGE_SIZE) -1));
  close(fd);
  if (mapaddress == MAP_FAILED)
    return NULL;
  return (void *)(mapaddress+(offset & (sysconf(_SC_PAGE_SIZE) -1)));
}

void clflush(volatile void* Tx) {
    asm volatile("lfence;clflush (%0) \n" :: "c" (Tx));
}

uint32_t reload(void *target)
{
    volatile uint32_t time;
    uint64_t t1, t2;
    t1 = timer_start();
    maccess(target);
    t2 = timer_stop();
    return t2 - t1;
}

void unmap_offset(void *address) {
  munmap((char *)(((uintptr_t)address) & ~(sysconf(_SC_PAGE_SIZE) -1)),
                sysconf(_SC_PAGE_SIZE));
}

int main()
{
    FILE* fp_cache_hit = fopen("timing_data_cache_hit", "wb");
    FILE* fp_cache_miss = fopen("timing_data_cache_miss", "wb");
    uint32_t num_traces = 1000000;
    size_t offset = 0;
    // target = malloc(sizeof(uint32_t));
    timing_data = (uint32_t*) malloc(sizeof(uint32_t));
    // target = map_offset("openssl/libcrypto.so", offset);
    void* addr = malloc(10);
    // //testing cache hits
    maccess(addr);

    for (int i = 0; i < num_traces; i++) 
    {
        *timing_data = reload(addr);
        fprintf(fp_cache_hit, "%d\n", *timing_data);
    }
  
    for (int i = 0; i < num_traces; i++) 
    {
        clflush( addr);
        *timing_data = reload( addr);
        fprintf(fp_cache_miss, "%d\n", *timing_data);

    }

    fclose(fp_cache_hit);
    fclose(fp_cache_miss);
    unmap_offset(addr);

}
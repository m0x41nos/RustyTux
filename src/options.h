#ifndef ESPINTCP_OPTIONS_H
#define ESPINTCP_OPTIONS_H

#include <stdbool.h>
#include <stdint.h>

#define DEFAULT_FILLER_FRAMES 1024ULL
#define DEFAULT_PRESTACK_FRAMES 200ULL
#define DEFAULT_PRESTACK_POLL_US 1000ULL
#define DEFAULT_CLOSE_DELAY_NS 3600000ULL
#define DEFAULT_TRIGGER_DELAY_NS 10000ULL
#define DEFAULT_RCVTIMEO_US 500000ULL
#define DEFAULT_MAX_WINDOWS 200ULL

enum final_mode {
	FINAL_ONE_BYTE_HEADER,
	FINAL_INCOMPLETE_BODY,
};

enum driver_mode {
	DRIVER_FIXED_TIMING,
	DRIVER_SCHEDULE,
};

struct options {
	uint64_t filler_frames;
	uint64_t prestack_frames;
	uint64_t prestack_poll_us;
	uint64_t close_delay_ns;
	uint64_t trigger_delay_ns;
	uint64_t rcvtimeo_us;
	int sender_cpu;
	int closer_cpu;
	bool quiet;
	enum final_mode final_mode;
};

struct driver_config {
	enum driver_mode mode;
	bool aggressive;
	bool max_windows_set;
	uint64_t max_windows;
};

void usage(const char *prog);
void init_options(struct options *opts);
void init_driver_config(struct driver_config *cfg);
int parse_driver_options(int argc, char **argv, struct options *opts,
			 struct driver_config *cfg);

#endif

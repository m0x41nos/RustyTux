#include "options.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

static int parse_u64_arg(const char *s, uint64_t *out)
{
	char *end = NULL;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno || !end || *end != '\0')
		return -1;

	*out = (uint64_t)v;
	return 0;
}

void init_options(struct options *opts)
{
	opts->filler_frames = DEFAULT_FILLER_FRAMES;
	opts->prestack_frames = DEFAULT_PRESTACK_FRAMES;
	opts->prestack_poll_us = DEFAULT_PRESTACK_POLL_US;
	opts->close_delay_ns = DEFAULT_CLOSE_DELAY_NS;
	opts->trigger_delay_ns = DEFAULT_TRIGGER_DELAY_NS;
	opts->rcvtimeo_us = DEFAULT_RCVTIMEO_US;
	opts->sender_cpu = -1;
	opts->closer_cpu = -1;
	opts->quiet = false;
	opts->final_mode = FINAL_ONE_BYTE_HEADER;
}

void init_driver_config(struct driver_config *cfg)
{
	cfg->mode = DRIVER_SCHEDULE;
	cfg->aggressive = false;
	cfg->max_windows_set = false;
	cfg->max_windows = DEFAULT_MAX_WINDOWS;
}

int parse_driver_options(int argc, char **argv, struct options *opts,
			 struct driver_config *cfg)
{
	enum {
		OPT_FIXED_TIMING = 1000,
		OPT_AGGRESSIVE,
		OPT_MAX_WINDOWS,
	};

	static const struct option long_opts[] = {
		{"fixed-timing", no_argument, NULL, OPT_FIXED_TIMING},
		{"aggressive", no_argument, NULL, OPT_AGGRESSIVE},
		{"max-windows", required_argument, NULL, OPT_MAX_WINDOWS},
		{"quiet", no_argument, NULL, 'q'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0},
	};

	for (;;) {
		int c = getopt_long(argc, argv, "qh", long_opts, NULL);

		if (c == -1)
			break;

		switch (c) {
		case OPT_FIXED_TIMING:
			cfg->mode = DRIVER_FIXED_TIMING;
			break;
		case OPT_AGGRESSIVE:
			cfg->aggressive = true;
			break;
		case OPT_MAX_WINDOWS:
			if (parse_u64_arg(optarg, &cfg->max_windows))
				return -1;
			cfg->max_windows_set = true;
			break;
		case 'q':
			opts->quiet = true;
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
		default:
			return -1;
		}
	}

	if (optind != argc)
		return -1;
	if (cfg->aggressive && cfg->mode != DRIVER_SCHEDULE)
		return -1;
	if (!cfg->max_windows_set)
		cfg->max_windows = DEFAULT_MAX_WINDOWS;
	if (opts->rcvtimeo_us == 0) {
		fprintf(stderr, "[-] SO_RCVTIMEO must be finite and non-zero for this trigger.\n");
		return -1;
	}
	if (opts->filler_frames == 0) {
		fprintf(stderr, "[-] at least one complete filler frame is required for streaming-prefix timing.\n");
		return -1;
	}

	return 0;
}

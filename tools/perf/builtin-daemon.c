// SPDX-License-Identifier: GPL-2.0
#include <subcmd/parse-options.h>
#include <api/fd/array.h>
#include <linux/limits.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <poll.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "config.h"
#include "util.h"

struct daemon {
	const char		*config;
	char			*config_real;
	char			*base;
	FILE			*out;
	char			 perf[PATH_MAX];
};

static struct daemon __daemon = { };

static const char * const daemon_usage[] = {
	"perf daemon start [<options>]",
	"perf daemon [<options>]",
	NULL
};

static bool done;

static void sig_handler(int sig __maybe_unused)
{
	done = true;
}

static int setup_server_socket(struct daemon *daemon)
{
	struct sockaddr_un addr;
	char path[100];
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		fprintf(stderr, "socket: %s\n", strerror(errno));
		return -1;
	}

	fcntl(fd, F_SETFD, FD_CLOEXEC);

	scnprintf(path, PATH_MAX, "%s/control", daemon->base);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;

	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	unlink(path);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		perror("failed: bind");
		return -1;
	}

	if (listen(fd, 1) == -1) {
		perror("failed: listen");
		return -1;
	}

	return fd;
}

union cmd {
	int cmd;
};

static int handle_server_socket(struct daemon *daemon __maybe_unused, int sock_fd)
{
	int ret = -EINVAL, fd;
	union cmd cmd;
	FILE *out;

	fd = accept(sock_fd, NULL, NULL);
	if (fd < 0) {
		fprintf(stderr, "accept: %s\n", strerror(errno));
		return -1;
	}

	if (sizeof(cmd) != read(fd, &cmd, sizeof(cmd))) {
		fprintf(stderr, "read: %s\n", strerror(errno));
		return -1;
	}

	out = fdopen(fd, "w");
	if (!out) {
		perror("failed: fdopen");
		return -1;
	}

	switch (cmd.cmd) {
	default:
		break;
	}

	fclose(out);
	close(fd);
	return ret;
}

static void daemon__free(struct daemon *daemon)
{
	free(daemon->config_real);
}

static void daemon__exit(struct daemon *daemon)
{
	daemon__free(daemon);
}

static int setup_config(struct daemon *daemon)
{
	if (daemon->config) {
		char *real = realpath(daemon->config, NULL);

		if (!real) {
			perror("failed: realpath");
			return -1;
		}
		daemon->config_real = real;
		return 0;
	}

	if (perf_config_system() && !access(perf_etc_perfconfig(), R_OK))
		daemon->config_real = strdup(perf_etc_perfconfig());
	else if (perf_config_global() && perf_home_perfconfig())
		daemon->config_real = strdup(perf_home_perfconfig());

	return daemon->config_real ? 0 : -1;
}

static int __cmd_start(struct daemon *daemon, struct option parent_options[],
		       int argc, const char **argv)
{
	struct option start_options[] = {
		OPT_PARENT(parent_options),
		OPT_END()
	};
	int sock_pos, sock_fd;
	struct fdarray fda;
	int err = 0;

	argc = parse_options(argc, argv, start_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, start_options);

	if (setup_config(daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	debug_set_file(daemon->out);
	debug_set_display_time(true);

	pr_info("daemon started (pid %d)\n", getpid());

	sock_fd = setup_server_socket(daemon);
	if (sock_fd < 0)
		return -1;

	fdarray__init(&fda, 1);

	sock_pos = fdarray__add(&fda, sock_fd, POLLIN|POLLERR|POLLHUP, 0);
	if (sock_pos < 0)
		return -1;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	while (!done && !err) {
		if (fdarray__poll(&fda, -1)) {
			if (fda.entries[sock_pos].revents & POLLIN)
				err = handle_server_socket(daemon, sock_fd);
		}
	}

	fdarray__exit(&fda);

	daemon__exit(daemon);

	close(sock_fd);

	pr_info("daemon exited\n");
	fclose(daemon->out);
	return err;
}

int cmd_daemon(int argc, const char **argv)
{
	struct option daemon_options[] = {
		OPT_INCR('v', "verbose", &verbose, "be more verbose"),
		OPT_STRING(0, "config", &__daemon.config,
			"config file", "config file path"),
		OPT_END()
	};

	perf_exe(__daemon.perf, sizeof(__daemon.perf));
	__daemon.out = stdout;

	argc = parse_options(argc, argv, daemon_options, daemon_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc && !strcmp(argv[0], "start"))
		return __cmd_start(&__daemon, daemon_options, argc, argv);

	if (argc) {
		pr_err("failed: unknown command '%s'\n", argv[0]);
		return -1;
	}

	if (setup_config(&__daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	return -1;
}

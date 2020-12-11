// SPDX-License-Identifier: GPL-2.0
#include <subcmd/parse-options.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/zalloc.h>
#include <linux/limits.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <api/fd/array.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <libgen.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "config.h"
#include "string2.h"
#include "asm/bug.h"
#include <api/fs/fs.h>

#define SESSION_OUTPUT  "output"

enum session_state {
	SESSION_STATE__OK,
	SESSION_STATE__RECONFIG,
	SESSION_STATE__KILL,
};

struct session {
	char			*name;
	char			*run;
	int			 pid;
	struct list_head	 list;
	enum session_state	 state;
};

struct daemon {
	const char		*config;
	const char		*csv_sep;
	char			*config_real;
	char			*config_base;
	char			*base;
	struct list_head	 sessions;
	FILE			*out;
};

static struct daemon __daemon = {
	.sessions = LIST_HEAD_INIT(__daemon.sessions),
};

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

static struct session*
daemon__add_session(struct daemon *config, char *name)
{
	struct session *session;

	session = zalloc(sizeof(*session));
	if (!session)
		return NULL;

	session->name = strdup(name);
	if (!session->name) {
		free(session);
		return NULL;
	}

	session->pid = -1;
	list_add_tail(&session->list, &config->sessions);
	return session;
}

static struct session*
daemon__find_session(struct daemon *daemon, char *name)
{
	struct session *session;

	list_for_each_entry(session, &daemon->sessions, list) {
		if (!strcmp(session->name, name))
			return session;
	}

	return NULL;
}

static int session_name(const char *var, char *session, int len)
{
	const char *p = var + sizeof("session-") - 1;

	while (*p != '.' && len--)
		*session++ = *p++;

	*session = 0;
	return *p == '.' ? 0 : -EINVAL;
}

static int session_config(struct daemon *daemon, const char *var, const char *value)
{
	struct session *session;
	char name[100];

	if (session_name(var, name, sizeof(name)))
		return -EINVAL;

	var = strchr(var, '.');
	if (!var)
		return -EINVAL;

	var++;

	session = daemon__find_session(daemon, name);
	if (!session) {
		session = daemon__add_session(daemon, name);
		if (!session)
			return -ENOMEM;

		pr_debug("reconfig: found new session %s\n", name);
		/* This is new session, trigger reconfig to start it. */
		session->state = SESSION_STATE__RECONFIG;
	} else if (session->state == SESSION_STATE__KILL) {
		/*
		 * The session was marked to kill and we still
		 * found it in config file.
		 */
		pr_debug("reconfig: found current session %s\n", name);
		session->state = SESSION_STATE__OK;
	}

	if (!strcmp(var, "run")) {
		if (session->run && strcmp(session->run, value)) {
			free(session->run);
			pr_debug("reconfig: session %s is changed\n", name);
			session->state = SESSION_STATE__RECONFIG;
		}
		session->run = strdup(value);
	}

	return 0;
}

static int server_config(const char *var, const char *value, void *cb)
{
	struct daemon *daemon = cb;

	if (strstarts(var, "session-"))
		return session_config(daemon, var, value);
	else if (!strcmp(var, "daemon.base"))
		daemon->base = strdup(value);

	return 0;
}

static int client_config(const char *var, const char *value, void *cb)
{
	struct daemon *daemon = cb;

	if (!strcmp(var, "daemon.base"))
		daemon->base = strdup(value);

	return 0;
}

static int setup_client_config(struct daemon *daemon)
{
	struct perf_config_set *set;
	int err = -ENOMEM;

	set = perf_config_set__load_file(daemon->config);
	if (set) {
		err = perf_config_set(set, client_config, daemon);
		perf_config_set__delete(set);
	}

	return err;
}

static int setup_server_config(struct daemon *daemon)
{
	struct perf_config_set *set;
	struct session *session;
	int err = -ENOMEM;

	pr_debug("reconfig: started\n");

	/*
	 * Mark all session for kill, the server config will
	 * set proper state for found sessions.
	 */
	list_for_each_entry(session, &daemon->sessions, list)
		session->state = SESSION_STATE__KILL;

	set = perf_config_set__load_file(daemon->config);
	if (set) {
		err = perf_config_set(set, server_config, daemon);
		perf_config_set__delete(set);
	}

	return err;
}

static int session__check(struct session *session, struct daemon *daemon)
{
	int err, status;

	err = waitpid(session->pid, &status, WNOHANG);
	if (err < 0) {
		session->pid = -1;
		return -1;
	}

	if (err && WIFEXITED(status)) {
		fprintf(daemon->out, "session(%d) %s excited with %d\n",
			session->pid, session->name, WEXITSTATUS(status));
		session->state = SESSION_STATE__KILL;
		session->pid = -1;
		return -1;
	}

	return 0;
}

static int session__wait(struct session *session, struct daemon *daemon,
			 int secs)
{
	time_t current, start = 0;
	int cnt;

	start = current = time(NULL);

	do {
		usleep(500);
		cnt = session__check(session, daemon);
		if (cnt)
			break;

		current = time(NULL);
	} while ((start + secs > current));

	return cnt;
}

static int session__signal(struct session *session, int sig)
{
	if (session->pid < 0)
		return -1;
	return kill(session->pid, sig);
}

static void session__kill(struct session *session, struct daemon *daemon)
{
	session__signal(session, SIGTERM);
	if (session__wait(session, daemon, 30))
		session__signal(session, SIGKILL);
}

static int session__run(struct session *session, struct daemon *daemon)
{
	char base[PATH_MAX];
	char buf[PATH_MAX];
	char **argv;
	int argc, fd;

	scnprintf(base, PATH_MAX, "%s/%s", daemon->base, session->name);

	if (mkdir(base, 0755) && errno != EEXIST) {
		perror("mkdir failed");
		return -1;
	}

	session->pid = fork();
	if (session->pid < 0)
		return -1;
	if (session->pid > 0) {
		pr_info("reconfig: ruining session [%s:%d]: %s\n",
			session->name, session->pid, session->run);
		return 0;
	}

	if (chdir(base)) {
		perror("chdir failed");
		return -1;
	}

	fd = open(SESSION_OUTPUT, O_RDWR|O_CREAT|O_TRUNC, 0644);
	if (fd < 0) {
		perror("open failed");
		return -1;
	}

	close(0);
	dup2(fd, 1);
	dup2(fd, 2);
	close(fd);

	scnprintf(buf, sizeof(buf), "%s record %s", PERF, session->run);

	argv = argv_split(buf, &argc);
	if (!argv)
		exit(-1);

	exit(execve(PERF, argv, NULL));
	return -1;
}

static int daemon__check(struct daemon *daemon)
{
	struct session *session;
	int cnt = 0;

	list_for_each_entry(session, &daemon->sessions, list) {
		if (session__check(session, daemon))
			continue;
		cnt++;
	}

	return cnt;
}

static int daemon__wait(struct daemon *daemon, int secs)
{
	time_t current, start = 0;
	int cnt;

	start = current = time(NULL);

	do {
		usleep(100);
		cnt = daemon__check(daemon);
		if (!cnt)
			break;

		current = time(NULL);
	} while ((start + secs > current));

	return cnt;
}

static void daemon__signal(struct daemon *daemon, int sig)
{
	struct session *session;

	list_for_each_entry(session, &daemon->sessions, list)
		session__signal(session, sig);
}

static void session__free(struct session *session)
{
	free(session->name);
	free(session->run);
	free(session);
}

static void session__remove(struct session *session)
{
	list_del(&session->list);
	session__free(session);
}

static int daemon__reconfig(struct daemon *daemon)
{
	struct session *session, *n;

	list_for_each_entry_safe(session, n, &daemon->sessions, list) {
		/* No change. */
		if (session->state == SESSION_STATE__OK)
			continue;

		/* Remove session. */
		if (session->state == SESSION_STATE__KILL) {
			if (session->pid > 0) {
				session__kill(session, daemon);
				pr_info("reconfig: session '%s' killed\n", session->name);
			}
			session__remove(session);
			continue;
		}

		/* Reconfig session. */
		pr_debug2("reconfig: session '%s' start\n", session->name);
		if (session->pid > 0) {
			session__kill(session, daemon);
			pr_info("reconfig: session '%s' killed\n", session->name);
		}
		if (session__run(session, daemon))
			return -1;
		pr_debug2("reconfig: session '%s' done\n", session->name);
		session->state = SESSION_STATE__OK;
	}

	return 0;
}

static void daemon__kill(struct daemon *daemon)
{
	daemon__signal(daemon, SIGTERM);
	if (daemon__wait(daemon, 30))
		daemon__signal(daemon, SIGKILL);
}

static void daemon__free(struct daemon *daemon)
{
	struct session *session, *h;

	list_for_each_entry_safe(session, h, &daemon->sessions, list)
		session__remove(session);

	free(daemon->config_real);
}

static void daemon__exit(struct daemon *daemon)
{
	daemon__kill(daemon);
	daemon__free(daemon);
	fclose(daemon->out);
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
		perror("bind error");
		return -1;
	}

	if (listen(fd, 1) == -1) {
		perror("listen error");
		return -1;
	}

	return fd;
}

enum {
	CMD_LIST = 0,
	CMD_SIGNAL = 1,
	CMD_STOP = 2,
	CMD_MAX,
};

#define SESSION_MAX 64

union cmd {
	int cmd;

	/* CMD_LIST */
	struct {
		int	cmd;
		int	verbose;
		char	csv_sep;
	} list;

	/* CMD_LIST */
	struct {
		int	cmd;
		int	sig;
		char	name[SESSION_MAX];
	} signal;
};

static int cmd_session_list(struct daemon *daemon, union cmd *cmd, FILE *out)
{
	char csv_sep = cmd->list.csv_sep;
	struct session *session;

	if (csv_sep) {
		fprintf(out, "%d%c%s%c%s%c%s/%s",
			/* pid daemon  */
			getpid(), csv_sep, "daemon",
			/* base */
			csv_sep, daemon->base,
			/* output */
			csv_sep, daemon->base, SESSION_OUTPUT);

		fprintf(out, "\n");
	} else {
		fprintf(out, "[%d:daemon] base: %s\n", getpid(), daemon->base);
		if (cmd->list.verbose) {
			fprintf(out, "  output:  %s/%s\n",
				SESSION_OUTPUT, daemon->base);
		}
	}

	list_for_each_entry(session, &daemon->sessions, list) {
		if (csv_sep) {
			fprintf(out, "%d%c%s%c%s",
				/* pid */
				session->pid,
				/* name */
				csv_sep, session->name,
				/* base */
				csv_sep, session->run);

			fprintf(out, "%c%s/%s%c%s/%s/%s",
				/* session dir */
				csv_sep, daemon->base, session->name,
				/* session output */
				csv_sep, daemon->base, session->name, SESSION_OUTPUT);

			fprintf(out, "\n");
		} else {
			fprintf(out, "[%d:%s] perf record %s\n",
				session->pid, session->name, session->run);
			if (!cmd->list.verbose)
				continue;
			fprintf(out, "  base:    %s/%s\n",
				daemon->base, session->name);
			fprintf(out, "  output:  %s/%s/%s\n",
				daemon->base, session->name, SESSION_OUTPUT);
		}
	}

	return 0;
}

static int cmd_session_kill(struct daemon *daemon, union cmd *cmd, FILE *out)
{
	struct session *session;
	bool all = false;

	all = !strcmp(cmd->signal.name, "all");

	list_for_each_entry(session, &daemon->sessions, list) {
		if (all || !strcmp(cmd->signal.name, session->name)) {
			session__signal(session, cmd->signal.sig);
			fprintf(out, "signal %d sent to session '%s [%d]'\n",
				cmd->signal.sig, session->name, session->pid);
		}
	}

	return 0;
}

static int handle_server_socket(struct daemon *daemon, int sock_fd)
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
		perror("fopen");
		return -1;
	}

	switch (cmd.cmd) {
	case CMD_LIST:
		ret = cmd_session_list(daemon, &cmd, out);
		break;
	case CMD_SIGNAL:
		ret = cmd_session_kill(daemon, &cmd, out);
		break;
	case CMD_STOP:
		done = 1;
		pr_debug("perf daemon is exciting\n");
		break;
	default:
		break;
	}

	fclose(out);
	close(fd);
	return ret;
}

static int setup_client_socket(struct daemon *daemon)
{
	struct sockaddr_un addr;
	char path[100];
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		perror("socket error");
		return -1;
	}

	scnprintf(path, PATH_MAX, "%s/control", daemon->base);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		perror("connect error");
		return -1;
	}

	return fd;
}

static int setup_config_changes(struct daemon *daemon)
{
	char *basen = strdup(daemon->config);
	char *dirn  = strdup(daemon->config);
	char *base, *dir;
	int fd, wd;

	if (!dirn || !basen)
		return -ENOMEM;

	fd = inotify_init1(IN_NONBLOCK|O_CLOEXEC);
	if (fd < 0) {
		perror("inotify_init failed");
		return -1;
	}

	dir = dirname(dirn);
	base = basename(basen);
	pr_debug("config file: %s, dir: %s\n", base, dir);

	wd = inotify_add_watch(fd, dir, IN_CLOSE_WRITE);
	if (wd < 0)
		perror("inotify_add_watch failed");
	else
		daemon->config_base = base;

	free(dirn);
	return wd < 0 ? -1 : fd;
}

static bool process_inotify_event(struct daemon *daemon, char *buf, ssize_t len)
{
	char *p = buf;

	while (p < (buf + len)) {
		struct inotify_event *event = (struct inotify_event *) p;

		/*
		 * We monitor config directory, check if our
		 * config file was changes.
		 */
		if ((event->mask & IN_CLOSE_WRITE) &&
		    !(event->mask & IN_ISDIR)) {
			if (!strcmp(event->name, daemon->config_base))
				return true;
		}
		p += sizeof(*event) + event->len;
	}
	return false;
}

static int handle_config_changes(struct daemon *daemon, int conf_fd,
				 bool *config_changed)
{
	char buf[4096];
	ssize_t len;

	while (!(*config_changed)) {
		len = read(conf_fd, buf, sizeof(buf));
		if (len == -1) {
			if (errno != EAGAIN) {
				perror("read failed");
				return -1;
			}
			return 0;
		}
		*config_changed = process_inotify_event(daemon, buf, len);
	}
	return 0;
}

static int go_background(struct daemon *daemon)
{
	int pid, fd;

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid > 0)
		return 1;

	if (setsid() < 0)
		return -1;

	umask(0);

	if (chdir(daemon->base)) {
		perror("chdir failed");
		return -1;
	}

	fd = open("output", O_RDWR|O_CREAT|O_TRUNC, 0644);
	if (fd < 0) {
		perror("open failed");
		return -1;
	}

	fcntl(fd, F_SETFD, FD_CLOEXEC);

	daemon->out = fdopen(fd, "w");
	if (!daemon->out)
		return -1;

	close(0);
	dup2(fd, 1);
	dup2(fd, 2);
	setbuf(daemon->out, NULL);
	return 0;
}

static int setup_daemon_config(struct daemon *daemon)
{
	if (daemon->config) {
		char *real = realpath(daemon->config, NULL);

		if (!real) {
			perror("realpath failed");
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
	bool foreground = false;
	struct option start_options[] = {
		OPT_BOOLEAN('f', "foreground", &foreground, "stay on console"),
		OPT_PARENT(parent_options),
		OPT_END()
	};
	int sock_pos, file_pos, sock_fd, conf_fd;
	bool reconfig = true;
	struct fdarray fda;
	int err = 0;

	argc = parse_options(argc, argv, start_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, start_options);

	if (setup_server_config(daemon))
		return -1;

	if (!foreground && go_background(daemon))
		return -1;

	debug_set_file(daemon->out);
	debug_set_display_time(true);

	pr_info("daemon started (pid %d)\n", getpid());

	sock_fd = setup_server_socket(daemon);
	if (sock_fd < 0)
		return -1;

	conf_fd = setup_config_changes(daemon);
	if (conf_fd < 0)
		return -1;

	/* socket, inotify */
	fdarray__init(&fda, 2);

	sock_pos = fdarray__add(&fda, sock_fd, POLLIN | POLLERR | POLLHUP, 0);
	if (sock_pos < 0)
		return -1;

	file_pos = fdarray__add(&fda, conf_fd, POLLIN | POLLERR | POLLHUP, 0);
	if (file_pos < 0)
		return -1;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	while (!done && !err) {
		if (reconfig) {
			err = daemon__reconfig(daemon);
			reconfig = false;
		}

		if (fdarray__poll(&fda, 500)) {
			if (fda.entries[sock_pos].revents & POLLIN)
				err = handle_server_socket(daemon, sock_fd);
			if (fda.entries[file_pos].revents & POLLIN)
				err = handle_config_changes(daemon, conf_fd, &reconfig);

			if (reconfig)
				err = setup_server_config(daemon);
		}

		if (!daemon__check(daemon)) {
			fprintf(daemon->out, "no sessions left, bailing out\n");
			break;
		}
	}

	pr_info("daemon exited\n");

	close(sock_fd);
	close(conf_fd);

	fdarray__exit(&fda);
	daemon__exit(daemon);
	return err;
}

static int send_cmd(struct daemon *daemon, union cmd *cmd)
{
	char *line = NULL;
	size_t len = 0;
	ssize_t nread;
	FILE *in;
	int fd;

	setup_client_config(daemon);

	fd = setup_client_socket(daemon);
	if (fd < 0)
		return -1;

	if (sizeof(*cmd) != write(fd, cmd, sizeof(*cmd)))
		return -1;

	in = fdopen(fd, "r");
	if (!in) {
		perror("fopen");
		return -1;
	}

	while ((nread = getline(&line, &len, in)) != -1) {
		fwrite(line, nread, 1, stdout);
		fflush(stdout);
	}

	close(fd);
	return 0;
}

static int send_cmd_list(struct daemon *daemon)
{
	union cmd cmd = {
		.list.cmd	= CMD_LIST,
		.list.verbose	= verbose,
	};

	cmd.list.csv_sep = daemon->csv_sep ? *daemon->csv_sep : 0;
	return send_cmd(daemon, &cmd);
}

static int __cmd_signal(struct daemon *daemon, struct option parent_options[],
			int argc, const char **argv)
{
	const char *sigstr = NULL;
	const char *name = "all";
	struct option start_options[] = {
		OPT_STRING(0, "session", &name, "session",
			"Sent signal to specific session"),
		OPT_STRING(0, "session", &sigstr, "signal",
			"Send specific signal"),
		OPT_PARENT(parent_options),
		OPT_END()
	};
	int sig = SIGUSR2;
	union cmd cmd;

	argc = parse_options(argc, argv, start_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, start_options);

	if (sigstr)
		sig = atoi(sigstr);

	cmd.signal.cmd = CMD_SIGNAL,
	cmd.signal.sig = sig;
	strncpy(cmd.signal.name, name, sizeof(cmd.signal.name) - 1);

	return send_cmd(daemon, &cmd);
}

static int __cmd_stop(struct daemon *daemon, struct option parent_options[],
			int argc, const char **argv)
{
	struct option start_options[] = {
		OPT_PARENT(parent_options),
		OPT_END()
	};
	union cmd cmd = { .cmd = CMD_STOP, };

	argc = parse_options(argc, argv, start_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, start_options);

	return send_cmd(daemon, &cmd);
}

int cmd_daemon(int argc, const char **argv)
{
	struct option daemon_options[] = {
		OPT_INCR('v', "verbose", &verbose, "be more verbose"),
		OPT_STRING(0, "config", &__daemon.config,
			"config file", "config file path"),
		OPT_STRING_OPTARG('x', "field-separator", &__daemon.csv_sep,
				  "field separator", "print counts with custom separator", ":"),
		OPT_END()
	};

	__daemon.out = stdout;

	argc = parse_options(argc, argv, daemon_options, daemon_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (setup_daemon_config(&__daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	if (argc && !strcmp(argv[0], "start"))
		return __cmd_start(&__daemon, daemon_options, argc, argv);

	if (argc) {
		if (!strcmp(argv[0], "signal")) {
			return __cmd_signal(&__daemon, daemon_options, argc, argv);
		} else if (!strcmp(argv[0], "stop")) {
			return __cmd_stop(&__daemon, daemon_options, argc, argv);
		} else {
			pr_err("failed: unknown command '%s'\n", argv[0]);
			return -1;
		}
	}

	return send_cmd_list(&__daemon);
}

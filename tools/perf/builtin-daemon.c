// SPDX-License-Identifier: GPL-2.0
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "config.h"
#include "string2.h"
#include <subcmd/parse-options.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/prctl.h>

struct daemon_session {
	char			*name;
	char			*run;
	int			 pid;
	struct list_head 	 list;
};

struct daemon {
	const char		*file;
	struct list_head	 sessions;
};

static int daemon__add_session(struct daemon *config,
			       char *name, const char *run)
{
	struct daemon_session *session;

	session = zalloc(sizeof(*session));
	if (!session)
		return -ENOMEM;

	session->name = strdup(name);
	session->run  = strdup(run);

	if (!session->name || !session->run) {
		free(session);
		return -ENOMEM;
	}

	list_add_tail(&session->list, &config->sessions);
	return 0;
}

static int perf_daemon_config(const char *var, const char *value, void *cb)
{
	struct daemon *config = cb;
	char *buf, *v, *name;

#define PREFIX "daemon-"

	if (strncmp(var, PREFIX, sizeof(PREFIX) - 1))
		return 0;

	buf = strdup(var);

	name = buf + sizeof(PREFIX) - 1;

	v = strchr(name, '.');
	if (!v)
		return -1;

	*v++ = 0;

	if (strcmp(v, "run"))
		return -1;

	return daemon__add_session(config, name, value);
}

static bool done;

static void sig_handler(int sig __maybe_unused)
{
	done = true;
}

static int wait_sessions(struct daemon *config)
{
	struct daemon_session *session;
	int status, cnt = 0;

	list_for_each_entry(session, &config->sessions, list) {
		int err;

		if (session->pid == -1)
			continue;

		err = waitpid(session->pid, &status, WNOHANG);
		if (err < 0) {
			session->pid = -1;
			continue;
		}

		if (err && WIFEXITED(status)) {
			fprintf(stdout, "session(%d) %s excited with %d\n",
				session->pid, session->name, WEXITSTATUS(status));
			session->pid = -1;
			continue;
		}

		cnt++;
	}

	return cnt;
}

static int wait_sessions_time(struct daemon *config, int secs)
{
	time_t current, start = 0;
	int cnt;

	start = current = time(NULL);

	do {
		usleep(500);
		cnt = wait_sessions(config);
		if (!cnt)
			break;

		current = time(NULL);
	} while ((start + secs > current));

	return cnt;
}

static void kill_sessions_sig(struct daemon *config, int sig)
{
	struct daemon_session *session;

	list_for_each_entry(session, &config->sessions, list) {
		if (session->pid == -1)
			continue;

		fprintf(stdout, "killing session(%d) %s with sig %d\n",
			session->pid, session->name, sig);
		kill(session->pid, sig);
	}
}

static void kill_sessions(struct daemon *config)
{
	kill_sessions_sig(config, SIGTERM);
	if (wait_sessions_time(config, 30))
		kill_sessions_sig(config, SIGKILL);
}

static int free_sessions(struct daemon *config)
{
	struct daemon_session *session, *h;

	list_for_each_entry_safe(session, h, &config->sessions, list) {
		list_del(&session->list);
		free(session->name);
		free(session->run);
		free(session);
	}

	return 0;
}

static int run(struct daemon_session *session)
{
	char **argv;
	int argc;

	session->pid = fork();
	if (session->pid > 0) {
		fprintf(stdout, "run session (%d): %s - %s\n",
			session->pid, session->name, session->run);
		return 0;
	} else if (session->pid < 0) {
		return -1;
	}

	argv = argv_split(session->run, &argc);
	if (!argv)
		exit(-1);

	prctl(PR_SET_NAME, "session");
	exit(cmd_record(argc, (const char **) argv));
	return -1;
}

static struct daemon config = {
	.file		= "/etc/perf-daemon.config",
	.sessions	= LIST_HEAD_INIT(config.sessions),
};

static struct option daemon_options[] = {
	OPT_STRING('c', "config", &config.file,
		   "config file", "config file path"),
	OPT_END()
};

static const char * const daemon_usage[] = {
	"perf daemon [<options>]",
	NULL
};

int cmd_daemon(int argc, const char **argv)
{
	struct daemon_session *session;
	struct perf_config_set *set;
	int err;

	argc = parse_options(argc, argv, daemon_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, daemon_options);

	set = perf_config_set__new_file(config.file);
	if (!set)
		return -ENOMEM;

	err = perf_config_set(set, perf_daemon_config, &config);
	if (err)
		return err;

	perf_config_set__delete(set);

	signal(SIGINT, sig_handler);

	err = -1;

	list_for_each_entry(session, &config.sessions, list) {
		if (run(session))
			goto out;
	}

	while (!done) {
		usleep(500);
		if (!wait_sessions(&config)) {
			fprintf(stdout, "no sessions left, bailing out\n");
			break;
		}
	}

	err = 0;

out:
	if (done || err)
		kill_sessions(&config);

	free_sessions(&config);
	return err;
}

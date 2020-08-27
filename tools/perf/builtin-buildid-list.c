/*
 * builtin-buildid-list.c
 *
 * Builtin buildid-list command: list buildids in perf.data, in the running
 * kernel and in ELF files.
 *
 * Copyright (C) 2009, Red Hat Inc.
 * Copyright (C) 2009, Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "builtin.h"
#include "perf.h"
#include "util/build-id.h"
#include "util/debug.h"
#include "util/dso.h"
#include <subcmd/pager.h>
#include <subcmd/parse-options.h>
#include "util/session.h"
#include "util/symbol.h"
#include "util/data.h"
#include "util/namespaces.h"
#include <errno.h>
#include <linux/err.h>
#include <linux/zalloc.h>
#ifdef HAVE_DEBUGINFOD_SUPPORT
#include <elfutils/debuginfod.h>
#endif
#include <unistd.h>
#include <sys/stat.h>

static int sysfs__fprintf_build_id(FILE *fp)
{
	char sbuild_id[SBUILD_ID_SIZE];
	int ret;

	ret = sysfs__sprintf_build_id("/", sbuild_id);
	if (ret != sizeof(sbuild_id))
		return ret < 0 ? ret : -EINVAL;

	return fprintf(fp, "%s\n", sbuild_id);
}

static int filename__fprintf_build_id(const char *name, FILE *fp)
{
	char sbuild_id[SBUILD_ID_SIZE];
	int ret;

	ret = filename__sprintf_build_id(name, sbuild_id);
	if (ret != sizeof(sbuild_id))
		return ret < 0 ? ret : -EINVAL;

	return fprintf(fp, "%s\n", sbuild_id);
}

static bool dso__skip_buildid(struct dso *dso, int with_hits)
{
	return with_hits && !dso->hit;
}

#ifdef HAVE_DEBUGINFOD_SUPPORT
static int get_executable(const char *sbuild_id, char **path)
{
	debuginfod_client *c;
	int fd;

	c = debuginfod_begin();
	if (c == NULL)
		return -1;

	pr_debug("trying debuginfod for executable <%s> ... ", sbuild_id);

	fd = debuginfod_find_executable(c, (const unsigned char*) sbuild_id,
					0, path);
	if (fd >= 0)
		close(fd); /* retaining reference by realname */

	debuginfod_end(c);
	pr_debug("%s%s\n", *path ? "OK " : "FAILED", *path ? *path : "");
	return *path ? 0 : -1;
}
#else
static int get_executable(const char *sbuild_id __maybe_unused,
			  char **path __maybe_unused)
{
	return -1;
}
#endif

struct dso_store_data {
	bool with_hits;
};

static int dso__store(struct dso *dso, struct machine *machine __maybe_unused, void *priv)
{
	struct dso_store_data *data = priv;
	char sbuild_id[SBUILD_ID_SIZE];
	u8 bid[BUILD_ID_SIZE];
	const char *path = NULL;
	bool is_kallsyms;
	int err;

	if (!dso->has_build_id ||
	    !build_id__is_defined(dso->build_id))
		return 0;

	if (data->with_hits && !dso->hit)
		return 0;

	is_kallsyms = !strcmp(machine->mmap_name, dso->short_name);

	build_id__sprintf(dso->build_id, sizeof(dso->build_id), sbuild_id);

	if (is_kallsyms) {
		path = strdup(dso->long_name);

		err = sysfs__read_build_id("/sys/kernel/notes", &bid, sizeof(bid));
		if (err < 0)
			goto out_err;
	} else {
		struct stat st;

		path = nsinfo__realpath(dso->long_name, dso->nsinfo);

		if (stat(path, &st)) {
			zfree(&path);
			goto try_download;
		}

		err = filename__read_build_id(path, &bid, sizeof(bid));
		if (err != sizeof(bid))
			goto out_err;
	}

	if (memcmp(&bid, dso->build_id, BUILD_ID_SIZE)) {
		char sbid[SBUILD_ID_SIZE];

		build_id__sprintf(bid, sizeof(bid), sbid);
		pr_debug("mmap build id <%s> does not match for %s <%s>\n",
			 sbuild_id, path, sbid);
		zfree(&path);
	}

try_download:
	if (!path) {
		char *tmp = NULL;

		if (get_executable(sbuild_id, &tmp)) {
			err = -1;
			goto out_err;
		}

		path = tmp;
		is_kallsyms = false;
	}

	pr_debug("linking %s %s <%s>\n", dso->short_name, path, sbuild_id);

	err = build_id_cache__add(sbuild_id, path, path,
				  dso->nsinfo, is_kallsyms, false);
out_err:
	fprintf(stderr, "%s %s %s\n", err ? "FAIL" : "OK  ", sbuild_id, dso->long_name);
	return 0;
}

static int perf_session__store(struct perf_session *session, bool with_hits)
{
	struct dso_store_data data = { .with_hits = with_hits, };

	return __perf_session__cache_build_ids(session, dso__store, &data);
}

static int perf_session__list_build_ids(bool force, bool with_hits, bool store)
{
	struct perf_session *session;
	struct perf_data data = {
		.path  = input_name,
		.mode  = PERF_DATA_MODE_READ,
		.force = force,
	};
	bool has_build_id;

	symbol__elf_init();
	/*
	 * See if this is an ELF file first:
	 */
	if (filename__fprintf_build_id(input_name, stdout) > 0)
		goto out;

	session = perf_session__new(&data, false, &build_id__mark_dso_hit_ops);
	if (IS_ERR(session))
		return PTR_ERR(session);

	/*
	 * We take all buildids when the file contains AUX area tracing data
	 * because we do not decode the trace because it would take too long.
	 */
	if (!perf_data__is_pipe(&data) &&
	    perf_header__has_feat(&session->header, HEADER_AUXTRACE))
		with_hits = false;

	has_build_id = perf_header__has_feat(&session->header, HEADER_BUILD_ID);

	/*
	 * We don't really show non hit dsos, keep that also for mmap3
	 * buildid data, we don't care about non hit dsos anyway.
	 */
	if (!has_build_id)
		with_hits = true;

	/*
	 * in pipe-mode, the only way to get the buildids is to parse
	 * the record stream. Buildids are stored as RECORD_HEADER_BUILD_ID
	 */
	if (with_hits || perf_data__is_pipe(&data))
		perf_session__process_events(session);

	if (store)
		perf_session__store(session, with_hits);
	else
		perf_session__fprintf_dsos_buildid(session, stdout, dso__skip_buildid, with_hits);

	perf_session__delete(session);
out:
	return 0;
}

int cmd_buildid_list(int argc, const char **argv)
{
	bool show_kernel = false;
	bool with_hits = false;
	bool force = false;
	bool store = false;
	const struct option options[] = {
	OPT_BOOLEAN('H', "with-hits", &with_hits, "Show only DSOs with hits"),
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_BOOLEAN('k', "kernel", &show_kernel, "Show current kernel build id"),
	OPT_BOOLEAN(0, "store", &store, "Store build id dsos in .debug cache"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};
	const char * const buildid_list_usage[] = {
		"perf buildid-list [<options>]",
		NULL
	};

	argc = parse_options(argc, argv, options, buildid_list_usage, 0);
	setup_pager();

	if (show_kernel)
		return !(sysfs__fprintf_build_id(stdout) > 0);

	return perf_session__list_build_ids(force, with_hits, store);
}

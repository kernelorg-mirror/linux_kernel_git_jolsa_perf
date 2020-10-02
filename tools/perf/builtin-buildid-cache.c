// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-buildid-cache.c
 *
 * Builtin buildid-cache command: Manages build-id cache
 *
 * Copyright (C) 2010, Red Hat Inc.
 * Copyright (C) 2010, Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include "builtin.h"
#include "namespaces.h"
#include "util/debug.h"
#include "util/header.h"
#include <subcmd/pager.h>
#include <subcmd/parse-options.h>
#include "util/strlist.h"
#include "util/build-id.h"
#include "util/session.h"
#include "util/dso.h"
#include "util/symbol.h"
#include "util/time-utils.h"
#include "util/util.h"
#include "util/probe-file.h"
#include <linux/string.h>
#include <linux/err.h>
#include <linux/zalloc.h>
#include <sys/stat.h>
#ifdef HAVE_DEBUGINFOD_SUPPORT
#include <elfutils/debuginfod.h>
#endif

static int build_id_cache__kcore_buildid(const char *proc_dir, char *sbuildid)
{
	char root_dir[PATH_MAX];
	char *p;

	strlcpy(root_dir, proc_dir, sizeof(root_dir));

	p = strrchr(root_dir, '/');
	if (!p)
		return -1;
	*p = '\0';
	return sysfs__sprintf_build_id(root_dir, sbuildid);
}

static int build_id_cache__kcore_dir(char *dir, size_t sz)
{
	return fetch_current_timestamp(dir, sz);
}

static bool same_kallsyms_reloc(const char *from_dir, char *to_dir)
{
	char from[PATH_MAX];
	char to[PATH_MAX];
	const char *name;
	u64 addr1 = 0, addr2 = 0;
	int i, err = -1;

	scnprintf(from, sizeof(from), "%s/kallsyms", from_dir);
	scnprintf(to, sizeof(to), "%s/kallsyms", to_dir);

	for (i = 0; (name = ref_reloc_sym_names[i]) != NULL; i++) {
		err = kallsyms__get_function_start(from, name, &addr1);
		if (!err)
			break;
	}

	if (err)
		return false;

	if (kallsyms__get_function_start(to, name, &addr2))
		return false;

	return addr1 == addr2;
}

static int build_id_cache__kcore_existing(const char *from_dir, char *to_dir,
					  size_t to_dir_sz)
{
	char from[PATH_MAX];
	char to[PATH_MAX];
	char to_subdir[PATH_MAX];
	struct dirent *dent;
	int ret = -1;
	DIR *d;

	d = opendir(to_dir);
	if (!d)
		return -1;

	scnprintf(from, sizeof(from), "%s/modules", from_dir);

	while (1) {
		dent = readdir(d);
		if (!dent)
			break;
		if (dent->d_type != DT_DIR)
			continue;
		scnprintf(to, sizeof(to), "%s/%s/modules", to_dir,
			  dent->d_name);
		scnprintf(to_subdir, sizeof(to_subdir), "%s/%s",
			  to_dir, dent->d_name);
		if (!compare_proc_modules(from, to) &&
		    same_kallsyms_reloc(from_dir, to_subdir)) {
			strlcpy(to_dir, to_subdir, to_dir_sz);
			ret = 0;
			break;
		}
	}

	closedir(d);

	return ret;
}

static int build_id_cache__add_kcore(const char *filename, bool force)
{
	char dir[32], sbuildid[SBUILD_ID_SIZE];
	char from_dir[PATH_MAX], to_dir[PATH_MAX];
	char *p;

	strlcpy(from_dir, filename, sizeof(from_dir));

	p = strrchr(from_dir, '/');
	if (!p || strcmp(p + 1, "kcore"))
		return -1;
	*p = '\0';

	if (build_id_cache__kcore_buildid(from_dir, sbuildid) < 0)
		return -1;

	scnprintf(to_dir, sizeof(to_dir), "%s/%s/%s",
		  buildid_dir, DSO__NAME_KCORE, sbuildid);

	if (!force &&
	    !build_id_cache__kcore_existing(from_dir, to_dir, sizeof(to_dir))) {
		pr_debug("same kcore found in %s\n", to_dir);
		return 0;
	}

	if (build_id_cache__kcore_dir(dir, sizeof(dir)))
		return -1;

	scnprintf(to_dir, sizeof(to_dir), "%s/%s/%s/%s",
		  buildid_dir, DSO__NAME_KCORE, sbuildid, dir);

	if (mkdir_p(to_dir, 0755))
		return -1;

	if (kcore_copy(from_dir, to_dir)) {
		/* Remove YYYYmmddHHMMSShh directory */
		if (!rmdir(to_dir)) {
			p = strrchr(to_dir, '/');
			if (p)
				*p = '\0';
			/* Try to remove buildid directory */
			if (!rmdir(to_dir)) {
				p = strrchr(to_dir, '/');
				if (p)
					*p = '\0';
				/* Try to remove [kernel.kcore] directory */
				rmdir(to_dir);
			}
		}
		return -1;
	}

	pr_debug("kcore added to build-id cache directory %s\n", to_dir);

	return 0;
}

static int build_id_cache__add_file(const char *filename, struct nsinfo *nsi)
{
	char sbuild_id[SBUILD_ID_SIZE];
	struct build_id bid;
	int err;
	struct nscookie nsc;

	nsinfo__mountns_enter(nsi, &nsc);
	err = filename__read_build_id(filename, &bid);
	nsinfo__mountns_exit(&nsc);
	if (err < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}

	build_id__sprintf(&bid, sbuild_id);
	err = build_id_cache__add_s(sbuild_id, filename, nsi,
				    false, false);
	pr_debug("Adding %s %s: %s\n", sbuild_id, filename,
		 err ? "FAIL" : "Ok");
	return err;
}

static int build_id_cache__remove_file(const char *filename, struct nsinfo *nsi)
{
	char sbuild_id[SBUILD_ID_SIZE];
	struct build_id bid;
	struct nscookie nsc;

	int err;

	nsinfo__mountns_enter(nsi, &nsc);
	err = filename__read_build_id(filename, &bid);
	nsinfo__mountns_exit(&nsc);
	if (err < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}

	build_id__sprintf(&bid, sbuild_id);
	err = build_id_cache__remove_s(sbuild_id);
	pr_debug("Removing %s %s: %s\n", sbuild_id, filename,
		 err ? "FAIL" : "Ok");

	return err;
}

static int build_id_cache__purge_path(const char *pathname, struct nsinfo *nsi)
{
	struct strlist *list;
	struct str_node *pos;
	int err;

	err = build_id_cache__list_build_ids(pathname, nsi, &list);
	if (err)
		goto out;

	strlist__for_each_entry(pos, list) {
		err = build_id_cache__remove_s(pos->s);
		pr_debug("Removing %s %s: %s\n", pos->s, pathname,
			 err ? "FAIL" : "Ok");
		if (err)
			break;
	}
	strlist__delete(list);

out:
	pr_debug("Purging %s: %s\n", pathname, err ? "FAIL" : "Ok");

	return err;
}

static int build_id_cache__purge_all(void)
{
	struct strlist *list;
	struct str_node *pos;
	int err = 0;
	char *buf;

	list = build_id_cache__list_all(false);
	if (!list) {
		pr_debug("Failed to get buildids: -%d\n", errno);
		return -EINVAL;
	}

	strlist__for_each_entry(pos, list) {
		buf = build_id_cache__origname(pos->s);
		err = build_id_cache__remove_s(pos->s);
		pr_debug("Removing %s (%s): %s\n", buf, pos->s,
			 err ? "FAIL" : "Ok");
		free(buf);
		if (err)
			break;
	}
	strlist__delete(list);

	pr_debug("Purged all: %s\n", err ? "FAIL" : "Ok");
	return err;
}

static bool dso__missing_buildid_cache(struct dso *dso, int parm __maybe_unused)
{
	char filename[PATH_MAX];
	struct build_id bid;

	if (dso__build_id_filename(dso, filename, sizeof(filename), false) &&
	    filename__read_build_id(filename, &bid) == -1) {
		if (errno == ENOENT)
			return false;

		pr_warning("Problems with %s file, consider removing it from the cache\n",
			   filename);
	} else if (memcmp(dso->bid.data, bid.data, bid.size)) {
		pr_warning("Problems with %s file, consider removing it from the cache\n",
			   filename);
	}

	return true;
}

static int build_id_cache__fprintf_missing(struct perf_session *session, FILE *fp)
{
	perf_session__fprintf_dsos_buildid(session, fp, dso__missing_buildid_cache, 0);
	return 0;
}

static int build_id_cache__update_file(const char *filename, struct nsinfo *nsi)
{
	char sbuild_id[SBUILD_ID_SIZE];
	struct build_id bid;
	struct nscookie nsc;

	int err;

	nsinfo__mountns_enter(nsi, &nsc);
	err = filename__read_build_id(filename, &bid);
	nsinfo__mountns_exit(&nsc);
	if (err < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}
	err = 0;

	build_id__sprintf(&bid, sbuild_id);
	if (build_id_cache__cached(sbuild_id))
		err = build_id_cache__remove_s(sbuild_id);

	if (!err)
		err = build_id_cache__add_s(sbuild_id, filename, nsi, false,
					    false);

	pr_debug("Updating %s %s: %s\n", sbuild_id, filename,
		 err ? "FAIL" : "Ok");

	return err;
}

static int build_id_cache__show_all(void)
{
	struct strlist *bidlist;
	struct str_node *nd;
	char *buf;

	bidlist = build_id_cache__list_all(true);
	if (!bidlist) {
		pr_debug("Failed to get buildids: -%d\n", errno);
		return -1;
	}
	strlist__for_each_entry(nd, bidlist) {
		buf = build_id_cache__origname(nd->s);
		fprintf(stdout, "%s %s\n", nd->s, buf);
		free(buf);
	}
	strlist__delete(bidlist);
	return 0;
}

#ifdef HAVE_DEBUGINFOD_SUPPORT
static int call_debuginfod(const char *sbuild_id, char **path, bool debuginfo)
{
	debuginfod_client *c;
	int fd;

	c = debuginfod_begin();
	if (c == NULL)
		return -1;

	pr_debug("trying debuginfod for executable <%s> ... ", sbuild_id);

	if (debuginfo) {
		fd = debuginfod_find_debuginfo(c, (const unsigned char *) sbuild_id,
					       0, path);
	} else {
		fd = debuginfod_find_executable(c, (const unsigned char *) sbuild_id,
						0, path);
	}
	if (fd >= 0)
		close(fd); /* retaining reference by realname */

	debuginfod_end(c);
	pr_debug("%s%s\n", *path ? "OK " : "FAILED", *path ? *path : "");
	return *path ? 0 : -1;
}
#else
static int call_debuginfod(const char *sbuild_id __maybe_unused,
			   char **path __maybe_unused,
			   bool debuginfo __maybe_unsed)
{
	return -1;
}
#endif

struct dso_store_data {
	bool	 hits;
	bool	 force_download;
};

static int dso_store(struct dso *dso, struct machine *machine, void *priv)
{
	struct dso_store_data *data = priv;
	char sbuild_id[SBUILD_ID_SIZE];
	struct build_id bid;
	char *path = NULL, *link = NULL;
	bool is_kallsyms;
	int err = -1;

	/*
	 * There's no build id in dso, nothing to do..
	 */
	if (!dso->has_build_id || !build_id__is_defined(&dso->bid))
		return 0;

	if (data->hits && !dso->hit)
		return 0;

	/*
	 * The storing process is:
	 *   - get build id of the dso
	 *   - check if it is already in cache
	 *   - check if it matches provided build id from mmap2 event
	 *   - if not, try debuginfod to download the binary
	 *   - store binary to build id database
	 */
	is_kallsyms = !strcmp(machine->mmap_name, dso->short_name);
	build_id__sprintf(&dso->bid, sbuild_id);

	link = build_id_cache__linkname(sbuild_id, NULL, 0);
	if (!link)
		return -ENOMEM;

	if (!data->force_download && !access(link, X_OK)) {
		pr_debug("already in cache - %s <%s>\n", dso->long_name, sbuild_id);
		err = 0;
		goto out;
	}

	path = strdup(dso->long_name);
	if (!path)
		goto out;

	if (is_kallsyms) {
		/*
		 * Find out if we are on the same kernel as perf.data
		 * and store kallsyms in that case.
		 */
		err = sysfs__read_build_id("/sys/kernel/notes", &bid);
		if (err < 0)
			goto out;
	} else {
		struct nscookie nsc;
		struct stat st;

		nsinfo__mountns_enter(dso->nsinfo, &nsc);

		/*
		 * Does the file exists in the first place, if it does,
		 * resolve path and read the build id.
		 */
		if (stat(dso->long_name, &st)) {
			nsinfo__mountns_exit(&nsc);
			zfree(&path);
			goto try_download;
		}

		err = filename__read_build_id(dso->long_name, &bid);
		nsinfo__mountns_exit(&nsc);

		if (err <= 0)
			goto out;
	}

	/*
	 * If we match, then what we want in mmap2 event
	 * is what we got in the binary,
	 */
	if (bid.size != dso->bid.size || memcmp(&bid, &dso->bid, bid.size)) {
		char sbid[SBUILD_ID_SIZE];

		build_id__sprintf(&bid, sbid);
		pr_debug("mmap build id <%s> does not match for %s <%s>\n",
			 sbuild_id, path, sbid);
		zfree(&path);
	}

try_download:
	/*
	 * We did not match build id or did not find the
	 * binary - try debuginfod as last resort.
	 */
	if (!path) {
		char *tmp = NULL;

		/*
		 * The debuginfo retrieval is handled within
		 * build_id_cache__add function.
		 */
		if (call_debuginfod(sbuild_id, &tmp, false) &&
		    call_debuginfod(sbuild_id, &tmp, true)) {
			err = -1;
			goto out;
		}

		path = tmp;

		/*
		 * The kernel dso is now elf binary, so disable is_kallsyms
		 * so build_id_cache__add can prepare proper file names.
		 */
		is_kallsyms = false;
	}

	pr_debug("linking %s %s <%s>\n", dso->short_name, path, sbuild_id);

	err = build_id_cache__add(sbuild_id, path, path,
				  dso->nsinfo, is_kallsyms, false);
out:
	free(path);
	fprintf(stderr, "%s %s %s\n", err ? "FAIL" : "OK  ", sbuild_id, dso->long_name);
	return 0;
}

static int
build_id_cache__add_perf_data(const char *path, bool all)
{
	struct perf_session *session;
	struct dso_store_data priv = {
		.hits = !all,
		.force_download = false,
	};
	struct perf_data data = {
		.path  = path,
		.mode  = PERF_DATA_MODE_READ,
	};
	int err;

	session = perf_session__new(&data, false, &build_id__mark_dso_hit_ops);
	if (IS_ERR(session))
		return PTR_ERR(session);

	err = perf_session__process_events(session);
	if (err)
		goto out;

	err = __perf_session__cache_build_ids(session, dso_store, &priv);
out:
	perf_session__delete(session);
	return err;
}

int cmd_buildid_cache(int argc, const char **argv)
{
	struct strlist *list;
	struct str_node *pos;
	int ret = 0;
	int ns_id = -1;
	bool force = false;
	bool list_files = false;
	bool opts_flag = false;
	bool purge_all = false;
	char const *add_name_list_str = NULL,
		   *remove_name_list_str = NULL,
		   *purge_name_list_str = NULL,
		   *missing_filename = NULL,
		   *update_name_list_str = NULL,
		   *kcore_filename = NULL;
	char sbuf[STRERR_BUFSIZE];

	struct perf_data data = {
		.mode  = PERF_DATA_MODE_READ,
	};
	struct perf_session *session = NULL;
	struct nsinfo *nsi = NULL;

	const struct option buildid_cache_options[] = {
	OPT_STRING('a', "add", &add_name_list_str,
		   "file list", "file(s) to add"),
	OPT_STRING('k', "kcore", &kcore_filename,
		   "file", "kcore file to add"),
	OPT_STRING('r', "remove", &remove_name_list_str, "file list",
		    "file(s) to remove"),
	OPT_STRING('p', "purge", &purge_name_list_str, "file list",
		    "file(s) to remove (remove old caches too)"),
	OPT_BOOLEAN('P', "purge-all", &purge_all, "purge all cached files"),
	OPT_BOOLEAN('l', "list", &list_files, "list all cached files"),
	OPT_STRING('M', "missing", &missing_filename, "file",
		   "to find missing build ids in the cache"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_STRING('u', "update", &update_name_list_str, "file list",
		    "file(s) to update"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_INTEGER(0, "target-ns", &ns_id, "target pid for namespace context"),
	OPT_END()
	};
	const char * const buildid_cache_usage[] = {
		"perf buildid-cache [<options>]",
		NULL
	};

	argc = parse_options(argc, argv, buildid_cache_options,
			     buildid_cache_usage, 0);

	opts_flag = add_name_list_str || kcore_filename ||
		remove_name_list_str || purge_name_list_str ||
		missing_filename || update_name_list_str ||
		purge_all;

	if (argc || !(list_files || opts_flag))
		usage_with_options(buildid_cache_usage, buildid_cache_options);

	/* -l is exclusive. It can not be used with other options. */
	if (list_files && opts_flag) {
		usage_with_options_msg(buildid_cache_usage,
			buildid_cache_options, "-l is exclusive.\n");
	}

	if (ns_id > 0)
		nsi = nsinfo__new(ns_id);

	if (missing_filename) {
		data.path  = missing_filename;
		data.force = force;

		session = perf_session__new(&data, false, NULL);
		if (IS_ERR(session))
			return PTR_ERR(session);
	}

	if (symbol__init(session ? &session->header.env : NULL) < 0)
		goto out;

	setup_pager();

	if (list_files) {
		ret = build_id_cache__show_all();
		goto out;
	}

	if (add_name_list_str) {
		list = strlist__new(add_name_list_str, NULL);
		if (list) {
			strlist__for_each_entry(pos, list)
				if (is_perf_data(pos->s)) {
					struct str_node *all_pos = strlist__next(pos);
					bool all = !strcmp("all", all_pos ? all_pos->s : "");

					if (build_id_cache__add_perf_data(pos->s, all))
						pr_warning("Couldn't add build ids from %s\n", pos->s);
					if (all)
						pos = all_pos;
				} else if (build_id_cache__add_file(pos->s, nsi)) {
					if (errno == EEXIST) {
						pr_debug("%s already in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't add %s: %s\n",
						   pos->s, str_error_r(errno, sbuf, sizeof(sbuf)));
				}
			strlist__delete(list);
		}
	}

	if (remove_name_list_str) {
		list = strlist__new(remove_name_list_str, NULL);
		if (list) {
			strlist__for_each_entry(pos, list)
				if (build_id_cache__remove_file(pos->s, nsi)) {
					if (errno == ENOENT) {
						pr_debug("%s wasn't in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't remove %s: %s\n",
						   pos->s, str_error_r(errno, sbuf, sizeof(sbuf)));
				}

			strlist__delete(list);
		}
	}

	if (purge_name_list_str) {
		list = strlist__new(purge_name_list_str, NULL);
		if (list) {
			strlist__for_each_entry(pos, list)
				if (build_id_cache__purge_path(pos->s, nsi)) {
					if (errno == ENOENT) {
						pr_debug("%s wasn't in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't remove %s: %s\n",
						   pos->s, str_error_r(errno, sbuf, sizeof(sbuf)));
				}

			strlist__delete(list);
		}
	}

	if (purge_all) {
		if (build_id_cache__purge_all()) {
			pr_warning("Couldn't remove some caches. Error: %s.\n",
				str_error_r(errno, sbuf, sizeof(sbuf)));
		}
	}

	if (missing_filename)
		ret = build_id_cache__fprintf_missing(session, stdout);

	if (update_name_list_str) {
		list = strlist__new(update_name_list_str, NULL);
		if (list) {
			strlist__for_each_entry(pos, list)
				if (build_id_cache__update_file(pos->s, nsi)) {
					if (errno == ENOENT) {
						pr_debug("%s wasn't in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't update %s: %s\n",
						   pos->s, str_error_r(errno, sbuf, sizeof(sbuf)));
				}

			strlist__delete(list);
		}
	}

	if (kcore_filename && build_id_cache__add_kcore(kcore_filename, force))
		pr_warning("Couldn't add %s\n", kcore_filename);

out:
	perf_session__delete(session);
	nsinfo__zput(nsi);

	return ret;
}

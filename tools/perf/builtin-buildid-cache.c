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
#include <asm/bug.h>
#include <linux/rbtree.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <ftw.h>
#include <time.h>
#include "builtin.h"
#include "perf.h"
#include "util/cache.h"
#include "util/debug.h"
#include "util/header.h"
#include "util/parse-options.h"
#include "util/strlist.h"
#include "util/build-id.h"
#include "util/session.h"
#include "util/symbol.h"

static int build_id_cache__kcore_buildid(const char *proc_dir, char *sbuildid)
{
	char root_dir[PATH_MAX];
	char notes[PATH_MAX];
	u8 build_id[BUILD_ID_SIZE];
	char *p;

	strlcpy(root_dir, proc_dir, sizeof(root_dir));

	p = strrchr(root_dir, '/');
	if (!p)
		return -1;
	*p = '\0';

	scnprintf(notes, sizeof(notes), "%s/sys/kernel/notes", root_dir);

	if (sysfs__read_build_id(notes, build_id, sizeof(build_id)))
		return -1;

	build_id__sprintf(build_id, sizeof(build_id), sbuildid);

	return 0;
}

static int build_id_cache__kcore_dir(char *dir, size_t sz)
{
	struct timeval tv;
	struct tm tm;
	char dt[32];

	if (gettimeofday(&tv, NULL) || !localtime_r(&tv.tv_sec, &tm))
		return -1;

	if (!strftime(dt, sizeof(dt), "%Y%m%d%H%M%S", &tm))
		return -1;

	scnprintf(dir, sz, "%s%02u", dt, (unsigned)tv.tv_usec / 10000);

	return 0;
}

static bool same_kallsyms_reloc(const char *from_dir, char *to_dir)
{
	char from[PATH_MAX];
	char to[PATH_MAX];
	const char *name;
	u64 addr1 = 0, addr2 = 0;
	int i;

	scnprintf(from, sizeof(from), "%s/kallsyms", from_dir);
	scnprintf(to, sizeof(to), "%s/kallsyms", to_dir);

	for (i = 0; (name = ref_reloc_sym_names[i]) != NULL; i++) {
		addr1 = kallsyms__get_function_start(from, name);
		if (addr1)
			break;
	}

	if (name)
		addr2 = kallsyms__get_function_start(to, name);

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

static int build_id_cache__add_kcore(const char *filename, const char *debugdir,
				     bool force)
{
	char dir[32], sbuildid[BUILD_ID_SIZE * 2 + 1];
	char from_dir[PATH_MAX], to_dir[PATH_MAX];
	char *p;

	strlcpy(from_dir, filename, sizeof(from_dir));

	p = strrchr(from_dir, '/');
	if (!p || strcmp(p + 1, "kcore"))
		return -1;
	*p = '\0';

	if (build_id_cache__kcore_buildid(from_dir, sbuildid))
		return -1;

	scnprintf(to_dir, sizeof(to_dir), "%s/[kernel.kcore]/%s",
		  debugdir, sbuildid);

	if (!force &&
	    !build_id_cache__kcore_existing(from_dir, to_dir, sizeof(to_dir))) {
		pr_debug("same kcore found in %s\n", to_dir);
		return 0;
	}

	if (build_id_cache__kcore_dir(dir, sizeof(dir)))
		return -1;

	scnprintf(to_dir, sizeof(to_dir), "%s/[kernel.kcore]/%s/%s",
		  debugdir, sbuildid, dir);

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

static int build_id_cache__add_file(const char *filename, const char *debugdir)
{
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];
	u8 build_id[BUILD_ID_SIZE];
	int err;

	if (filename__read_build_id(filename, &build_id, sizeof(build_id)) < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}

	build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	err = build_id_cache__add_s(sbuild_id, debugdir, filename,
				    false, false);
	if (verbose)
		pr_info("Adding %s %s: %s\n", sbuild_id, filename,
			err ? "FAIL" : "Ok");
	return err;
}

/*
 * Takes basename from @filename argument and
 * copy that into @buf.
 */
static int scnprintf_base(char *buf, int size, const char *filename)
{
	char *base = strrchr(filename, '/');

	if (!base++)
		return -1;

	return scnprintf(buf, size, base, strlen(base));
}

static bool is_kallsyms_file(const char *filename)
{
	return strstr(filename, "kernel.kallsyms");
}

static int build_id_cache__remove_file(const char *filename,
				       const char *debugdir)
{
	u8 build_id[BUILD_ID_SIZE];
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];

	int err;

	if (filename__read_build_id(filename, &build_id, sizeof(build_id)) < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);

		if (!is_kallsyms_file(filename))
			return -1;

		pr_debug("Detected [kernel.kallsyms] file, trying basename as buildid.\n");

		if (scnprintf_base(sbuild_id, sizeof(sbuild_id),
					     filename) < 0) {
			pr_debug("failed to get build-id\n");
			return -1;
		}
	} else {
		build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	}

	err = build_id_cache__remove_s(sbuild_id, debugdir);
	if (verbose)
		pr_info("Removing %s %s: %s\n", sbuild_id, filename,
			err ? "FAIL" : "Ok");

	return err;
}

static bool dso__missing_buildid_cache(struct dso *dso, int parm __maybe_unused)
{
	char filename[PATH_MAX];
	u8 build_id[BUILD_ID_SIZE];

	if (dso__build_id_filename(dso, filename, sizeof(filename)) &&
	    filename__read_build_id(filename, build_id,
				    sizeof(build_id)) != sizeof(build_id)) {
		if (errno == ENOENT)
			return false;

		pr_warning("Problems with %s file, consider removing it from the cache\n", 
			   filename);
	} else if (memcmp(dso->build_id, build_id, sizeof(dso->build_id))) {
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

static int build_id_cache__update_file(const char *filename,
				       const char *debugdir)
{
	u8 build_id[BUILD_ID_SIZE];
	char sbuild_id[BUILD_ID_SIZE * 2 + 1];

	int err;

	if (filename__read_build_id(filename, &build_id, sizeof(build_id)) < 0) {
		pr_debug("Couldn't read a build-id in %s\n", filename);
		return -1;
	}

	build_id__sprintf(build_id, sizeof(build_id), sbuild_id);
	err = build_id_cache__remove_s(sbuild_id, debugdir);
	if (!err) {
		err = build_id_cache__add_s(sbuild_id, debugdir, filename,
					    false, false);
	}
	if (verbose)
		pr_info("Updating %s %s: %s\n", sbuild_id, filename,
			err ? "FAIL" : "Ok");

	return err;
}

enum cache_sort {
	CACHE_SORT__NONE,
	CACHE_SORT__SIZE,
	CACHE_SORT__TIME,
};

enum cache_disp {
	CACHE_DISP__NONE,
	CACHE_DISP__ALL,
};

enum cache_limit {
	CACHE_LIMIT__NONE,
	CACHE_LIMIT__SIZE,
	CACHE_LIMIT__TIME,
};

enum cache_remove {
	CACHE_REMOVE__NONE,
	CACHE_REMOVE__SINGLE,
	CACHE_REMOVE__TOTAL,
};

struct cache_file {
	char		*path;
	u64		 size;
	time_t		 time;
	struct rb_node	 rb_node;
};

static struct rb_root cache_files;
static struct cache_file *cache_total;

static enum cache_sort cache_sort     = CACHE_SORT__NONE;
static enum cache_disp cache_disp     = CACHE_DISP__NONE;
static enum cache_limit cache_limit   = CACHE_LIMIT__NONE;
static enum cache_remove cache_remove = CACHE_REMOVE__NONE;

static time_t cache_limit__time;
static u64    cache_limit__size;

static struct cache_file*
cache_file__alloc(const char *path, const struct stat *st)
{
	struct cache_file *file = zalloc(sizeof(*file));

	if (file) {
		file->path = strdup(path);
		file->size = st ? st->st_size : 0;
		file->time = st ? st->st_atime : 0;
		RB_CLEAR_NODE(&file->rb_node);
	}
	return file;
}

static void cache_file__release(struct cache_file *file)
{
	free(file->path);
	free(file);
}

static int cmp_u64(u64 a, u64 b)
{
	return a > b ? -1 : a == b ? 0 : 1;
}

static int cache_file__cmp(struct cache_file *a, struct cache_file *b)
{
	switch (cache_sort) {
	case CACHE_SORT__SIZE:
		return cmp_u64(a->size, b->size);
	case CACHE_SORT__TIME:
		return cmp_u64((u64) a->time, (u64) b->time);
	case CACHE_SORT__NONE:
	default:
		pr_err("internal cache_sort bug\n");
	}
	return 0;
}

static void cache_files__add(struct cache_file *file)
{
	struct rb_node **p = &cache_files.rb_node;
	struct rb_node *parent = NULL;
	struct cache_file *n;

	while (*p != NULL) {
		parent = *p;
		n = rb_entry(parent, struct cache_file, rb_node);
		if (cache_file__cmp(n, file) >= 0)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&file->rb_node, parent, p);
	rb_insert_color(&file->rb_node, &cache_files);
}

typedef int (walk_cb_t)(struct cache_file *file, void *data);

static int cache_files__walk(walk_cb_t cb, void *data)
{
	struct rb_node *nd;
	int ret = 0;

	for (nd = rb_first(&cache_files); !ret && nd; nd = rb_next(nd)) {
		struct cache_file *n;

		n = rb_entry(nd, struct cache_file, rb_node);
		ret = cb(n, data);
	}

	return ret;
}

static int size_snprintf(u64 size, char *buf, int sz)
{
	struct {
		int div;
		const char *str;
	} suffix[] = {
		{ .str = "B", .div = 1 },
		{ .str = "K", .div = 1024 },
		{ .str = "M", .div = 1024*1024 },
		{ .str = "G", .div = 1024*1024*1024 },
	};
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(suffix); i++) {
		if (size / suffix[i].div < 1)
			break;
	}

	i--;
	return scnprintf(buf, sz, "%.1f%s",
			 (double) (size / suffix[i].div), suffix[i].str);
}

static int date_snprintf(time_t t, char *buf, int sz)
{
	struct tm tm;

	localtime_r(&t, &tm);
	return strftime(buf, sz, "%b %d", &tm);
}

static int cache_file__fprintf(FILE *out, struct cache_file *file)
{
	char size_buf[100];
	char date_buf[100];
	int ret = 0;

	if (cache_remove != CACHE_REMOVE__NONE)
		ret += fprintf(out, "Removed ");

	size_snprintf(file->size, size_buf, 100);
	date_snprintf(file->time, date_buf, 100);
	return ret + fprintf(out, "%10s  %6s  %s\n", size_buf, date_buf, file->path);
}

static int cache_file__process(struct cache_file *file, void *data)
{
	FILE *out = data;
	int ret = 0;

	if (cache_remove != CACHE_REMOVE__NONE)
		ret = build_id_cache__remove_file(file->path, buildid_dir);

	if (cache_disp == CACHE_DISP__ALL)
		cache_file__fprintf(out, file);

	return ret;
}

/*
 * We want to go through each file only if we remove or
 * display single files.
 */
static bool want_post_process(void)
{
	return (cache_remove == CACHE_REMOVE__SINGLE) ||
	       (cache_disp == CACHE_DISP__ALL);
}

static int cache_files__process(FILE *out)
{
	int ret = 0;

	/* Display total as first file/line. */
	cache_file__fprintf(out, cache_total);

	if (want_post_process())
		ret = cache_files__walk(cache_file__process, out);

	return ret;
}

static bool is_in_limit(const struct stat *st)
{
	bool in_limit = true;

	switch (cache_limit) {
	case CACHE_LIMIT__TIME:
		in_limit = st->st_atime <= cache_limit__time;
		break;
	case CACHE_LIMIT__SIZE:
		in_limit = (u64) st->st_size >= cache_limit__size;
		break;
	case CACHE_LIMIT__NONE:
	default:
		break;
	};

	return in_limit;
}

static int remove_file(const char *fpath, const struct stat *st)
{
	int ret;

	if (S_ISDIR(st->st_mode))
		ret = rmdir(fpath);
	else
		ret = unlink(fpath);

	if (ret)
		perror("failed to remove cache file");

	return ret;
}

static int nftw_cb(const char *fpath, const struct stat *st,
		   int typeflag __maybe_unused, struct FTW *ftwbuf)
{
	/* Do not touch the '.debug' directory itself. */
	if (!ftwbuf->level)
		return 0;

	/*
	 * Total cache wipe out handled right here. We try
	 * to remove everything despite the possible removal
	 * failures.
	 */
	if (cache_remove == CACHE_REMOVE__TOTAL) {
		cache_total->size += st->st_size;

		/* Ignore failure, remove as much as we can. */
		remove_file(fpath, st);
		return 0;
	}

	if (!is_in_limit(st))
		return 0;

	/* Sorting only regular files. */
	if (want_post_process() && S_ISREG(st->st_mode)) {
		struct cache_file *file;

		file = cache_file__alloc(fpath, st);
		if (!file)
			return -1;

		cache_files__add(file);
	}

	cache_total->size += st->st_size;
	return 0;
}

static int cache_files__alloc(void)
{
	int flags = FTW_PHYS;
	struct stat st;

	if (stat(buildid_dir, &st)) {
		pr_err("Failed to stat buildid directory %s.", buildid_dir);
		return -1;
	}

	cache_total = cache_file__alloc(buildid_dir, &st);
	if (!cache_total)
		return -1;

	/*
	 * If we're going to remove all the files, switch the walk
	 * files order to get inner directories/files first.  This
	 * way we can remove them immediately.
	 */
	if (cache_remove == CACHE_REMOVE__TOTAL)
		flags |= FTW_DEPTH;

	return nftw(buildid_dir, nftw_cb, 0, flags);
}

static int cache_file__remove(struct cache_file *file,
			      void *data __maybe_unused)
{
	rb_erase(&file->rb_node, &cache_files);
	cache_file__release(file);
	return 0;
}

static void cache_files__release(void)
{
	cache_files__walk(cache_file__remove, NULL);
	cache_file__release(cache_total);
}

static int setup_limit(char *limit)
{
	struct suffix {
		char s;
		long m;
	};
	struct suffix suffix_time[] = {
		{ .s = 'd', .m =   1*24*60*60 },
		{ .s = 'w', .m =   7*24*60*60 },
		{ .s = 'm', .m =  30*24*60*60 },
		{ .s = 'y', .m = 365*24*60*60 },
	};
	struct suffix suffix_size[] = {
		{ .s = 'B', .m = 1 },
		{ .s = 'K', .m = 1*1024 },
		{ .s = 'M', .m = 1*1024*1024 },
		{ .s = 'G', .m = 1*1024*1024*1024 },
	};
	char *suffix;
	long val;
	unsigned i;

	if (strlen(limit) < 2)
		return -1;

	val = strtol(limit, &suffix, 10);
	if (!suffix)
		return -1;

	if (strlen(suffix) != 1)
		return -1;

	for (i = 0; i < ARRAY_SIZE(suffix_time); i++) {
		char buf[100];

		if (suffix_time[i].s != suffix[0])
			continue;

		val *= -1 * suffix_time[i].m;
		val += time(0);
		cache_limit__time = val;
		cache_limit = CACHE_LIMIT__TIME;

		date_snprintf(cache_limit__time, buf, sizeof(buf));
		pr_debug("time limit: %s\n", buf);
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(suffix_size); i++) {
		char buf[100];

		if (suffix_size[i].s != suffix[0])
			continue;

		val *= suffix_size[i].m;
		cache_limit__size = val;
		cache_limit = CACHE_LIMIT__SIZE;

		size_snprintf(cache_limit__size, buf, sizeof(buf));
		pr_debug("size limit: %s\n", buf);
		return 0;
	}

	return -1;
}

static int cmd_buildid_cache_clean(int argc, const char **argv)
{
	const struct option buildid_cache_clean_options[] = {
	OPT_SET_UINT(0, "size", &cache_sort, "sort by size", CACHE_SORT__SIZE),
	OPT_SET_UINT(0, "time", &cache_sort, "sort by time", CACHE_SORT__TIME),
	OPT_SET_UINT('a', "all", &cache_disp, "display all files",
		     CACHE_DISP__ALL),
	OPT_SET_UINT('r', "remove", &cache_remove, "display all files",
		     CACHE_REMOVE__SINGLE),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END(),
	};
	const char * const buildid_cache_clean_usage[] = {
		"perf buildid-cache clean [<options>]",
		NULL,
	};
	int ret;

	argc = parse_options(argc, argv, buildid_cache_clean_options,
			     buildid_cache_clean_usage, 0);

	/* Check if user specified a limit. */
	if (argc) {
		char *limit = (char *) argv[0];

		if (argc != 1 || setup_limit(limit)) {
			pr_err("Failed: unsupported limit '%s'\n", limit);
			return -1;
		}
	}

	/* Full removal is handled separately. */
	if ((cache_remove == CACHE_REMOVE__SINGLE) &&
	    (cache_limit  == CACHE_LIMIT__NONE)    &&
	    (cache_disp   == CACHE_DISP__NONE)  &&
	    (cache_sort   == CACHE_SORT__NONE))
		cache_remove = CACHE_REMOVE__TOTAL;

	/*
	 * Sort by size by default and display all entries in case
	 * --size or --time option is specified.
	 */
	if (cache_sort == CACHE_SORT__NONE)
		cache_sort = CACHE_SORT__SIZE;
	else
		cache_disp = CACHE_DISP__ALL;

	if (cache_remove == CACHE_REMOVE__NONE)
		pr_warning("(mock mode, run with '-r' to actually remove data)\n");

	ret = cache_files__alloc();
	if (!ret)
		cache_files__process(stderr);

	cache_files__release();
	return ret;
}

static int process_subcmd(int argc, const char **argv)
{
	const char *cmd = argv[0];

	if (!strcmp(cmd, "clean"))
		return cmd_buildid_cache_clean(argc, argv);

	pr_err("Failed: unknown sub command '%s'\n", cmd);
	return -EINVAL;
}

int cmd_buildid_cache(int argc, const char **argv,
		      const char *prefix __maybe_unused)
{
	struct strlist *list;
	struct str_node *pos;
	int ret = 0;
	bool force = false;
	char const *add_name_list_str = NULL,
		   *remove_name_list_str = NULL,
		   *missing_filename = NULL,
		   *update_name_list_str = NULL,
		   *kcore_filename = NULL;
	char sbuf[STRERR_BUFSIZE];

	struct perf_data_file file = {
		.mode  = PERF_DATA_MODE_READ,
	};
	struct perf_session *session = NULL;

	const struct option buildid_cache_options[] = {
	OPT_STRING('a', "add", &add_name_list_str,
		   "file list", "file(s) to add"),
	OPT_STRING('k', "kcore", &kcore_filename,
		   "file", "kcore file to add"),
	OPT_STRING('r', "remove", &remove_name_list_str, "file list",
		    "file(s) to remove"),
	OPT_STRING('M', "missing", &missing_filename, "file",
		   "to find missing build ids in the cache"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_STRING('u', "update", &update_name_list_str, "file list",
		    "file(s) to update"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};
	const char * const buildid_cache_usage[] = {
		"perf buildid-cache [<options>]",
		NULL
	};

	argc = parse_options(argc, argv, buildid_cache_options,
			     buildid_cache_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (missing_filename) {
		file.path = missing_filename;
		file.force = force;

		session = perf_session__new(&file, false, NULL);
		if (session == NULL)
			return -1;
	}

	if (symbol__init(session ? &session->header.env : NULL) < 0)
		goto out;

	setup_pager();

	if (add_name_list_str) {
		list = strlist__new(true, add_name_list_str);
		if (list) {
			strlist__for_each(pos, list)
				if (build_id_cache__add_file(pos->s, buildid_dir)) {
					if (errno == EEXIST) {
						pr_debug("%s already in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't add %s: %s\n",
						   pos->s, strerror_r(errno, sbuf, sizeof(sbuf)));
				}

			strlist__delete(list);
		}
	}

	if (remove_name_list_str) {
		list = strlist__new(true, remove_name_list_str);
		if (list) {
			strlist__for_each(pos, list)
				if (build_id_cache__remove_file(pos->s, buildid_dir)) {
					if (errno == ENOENT) {
						pr_debug("%s wasn't in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't remove %s: %s\n",
						   pos->s, strerror_r(errno, sbuf, sizeof(sbuf)));
				}

			strlist__delete(list);
		}
	}

	if (missing_filename)
		ret = build_id_cache__fprintf_missing(session, stdout);

	if (update_name_list_str) {
		list = strlist__new(true, update_name_list_str);
		if (list) {
			strlist__for_each(pos, list)
				if (build_id_cache__update_file(pos->s, buildid_dir)) {
					if (errno == ENOENT) {
						pr_debug("%s wasn't in the cache\n",
							 pos->s);
						continue;
					}
					pr_warning("Couldn't update %s: %s\n",
						   pos->s, strerror_r(errno, sbuf, sizeof(sbuf)));
				}

			strlist__delete(list);
		}
	}

	if (kcore_filename &&
	    build_id_cache__add_kcore(kcore_filename, buildid_dir, force))
		pr_warning("Couldn't add %s\n", kcore_filename);

out:
	if (session)
		perf_session__delete(session);

	if (!ret && argc)
		ret = process_subcmd(argc, argv);

	return ret;
}

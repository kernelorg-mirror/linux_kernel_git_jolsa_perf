
/*
 * FIXME Missing test for PERF_RECORD_READ event.
 */

#include <stdlib.h>
#include <linux/kernel.h>

#include "tests.h"
#include "session.h"
#include "header.h"
#include "util.h"
#include "evlist.h"
#include "data.h"

#define EVENTS_MMAP 5
#define EVENTS_LOST 6
#define EVENTS_COMM 3
#define EVENTS_EXIT 4
#define EVENTS_FORK 8
#define EVENTS_THROTTLE 2
#define EVENTS_UNTHROTTLE 3
#define EVENTS_SAMPLE 4

char data_file_v2_le[] = {
#include "perf.data.v2.le.h"
};

char data_file_v2_be[] = {
#include "perf.data.v2.be.h"
};

char data_file_v3_le[] = {
#include "perf.data.v3.le.h"
};

static int events_mmap;
static int events_lost;
static int events_comm;
static int events_exit;
static int events_fork;
static int events_throttle;
static int events_unthrottle;
static int events_sample;

/* global sample type for sample */
static u64 sample_type = PERF_SAMPLE_IP |
			 PERF_SAMPLE_TID |
			 PERF_SAMPLE_TIME |
			 PERF_SAMPLE_ADDR |
			 PERF_SAMPLE_ID |
			 PERF_SAMPLE_CPU |
			 PERF_SAMPLE_PERIOD |
			 PERF_SAMPLE_STREAM_ID |
			 PERF_SAMPLE_WEIGHT |
			 PERF_SAMPLE_DATA_SRC;

static char *get_file(void)
{
	static char buf[PATH_MAX];
	int fd;

	snprintf(buf, PATH_MAX, "/tmp/perf-test-session-simple-data-XXXXXX");
	fd = mkstemp(buf);
	if (fd < 0) {
		pr_err("mkstemp failed");
		return NULL;
	}

	close(fd);

	pr_debug("data file %s\n", buf);
	return buf;
}

static union perf_event *get_event_MMAP(void)
{
	static union perf_event event;
	size_t size;

	size = snprintf(event.mmap.filename, sizeof(event.mmap.filename),
			"krava") + 1;
	size = PERF_ALIGN(size, sizeof(u64));

	event.header.type = PERF_RECORD_MMAP;
	event.header.misc = PERF_RECORD_MISC_KERNEL;
	event.header.size = sizeof(event.mmap) -
			    (sizeof(event.mmap.filename) - size);

	event.mmap.pgoff = 10;
	event.mmap.start = 0;
	event.mmap.len   = 10;
	event.mmap.pid   = 123;

	return &event;
}

static int process_mmap(struct perf_tool *tool __maybe_unused,
			union perf_event *event,
			struct perf_sample *sample __maybe_unused,
			struct machine *machine __maybe_unused)
{
	pr_debug("event MMAP misc %d, pgoff %lu, start %lu,"
		 " len %lu, pid %d, filename '%s'\n",
		 event->header.misc,
		 event->mmap.pgoff,
		 event->mmap.start,
		 event->mmap.len,
		 event->mmap.pid,
		 event->mmap.filename);

	TEST_ASSERT_VAL("wrong MMAP misc\n",
		event->header.misc == PERF_RECORD_MISC_KERNEL);

	TEST_ASSERT_VAL("wrong MMAP pgoff\n",
		event->mmap.pgoff == 10);

	TEST_ASSERT_VAL("wrong MMAP start\n",
		event->mmap.start == 0);

	TEST_ASSERT_VAL("wrong MMAP len\n",
		event->mmap.len == 10);

	TEST_ASSERT_VAL("wrong MMAP pid\n",
		event->mmap.pid == 123);

	TEST_ASSERT_VAL("wrong MMAP filename\n",
		!strcmp(event->mmap.filename, "krava"));

	TEST_ASSERT_VAL("wrong MMAP misc\n",
		event->header.misc == PERF_RECORD_MISC_KERNEL);

	TEST_ASSERT_VAL("wrong MMAP pgoff\n",
		event->mmap.pgoff == 10);

	TEST_ASSERT_VAL("wrong MMAP start\n",
		event->mmap.start == 0);

	TEST_ASSERT_VAL("wrong MMAP len\n",
		event->mmap.len == 10);

	TEST_ASSERT_VAL("wrong MMAP pid\n",
		event->mmap.pid == 123);

	TEST_ASSERT_VAL("wrong MMAP filename\n",
		!strcmp(event->mmap.filename, "krava"));

	events_mmap++;
	return 0;
}

static union perf_event *get_event_LOST(void)
{
	static union perf_event event;

	event.header.type = PERF_RECORD_LOST;
	event.header.misc = PERF_RECORD_MISC_USER;
	event.header.size = sizeof(event.lost);

	event.lost.id   = 12345;
	event.lost.lost = 293467890514143;
	return &event;
}

static int process_lost(struct perf_tool *tool __maybe_unused,
			union perf_event *event,
			struct perf_sample *sample __maybe_unused,
			struct machine *machine __maybe_unused)
{
	pr_debug("event LOST id %lu, lost %lu\n",
		 event->lost.id,
		 event->lost.lost);

	TEST_ASSERT_VAL("wrong LOST id\n",
		event->lost.id == 12345);

	TEST_ASSERT_VAL("wrong LOST lost\n",
		event->lost.lost == 293467890514143);

	events_lost++;
	return 0;
}

static union perf_event *get_event_COMM(void)
{
	static union perf_event event;
	size_t size;

	size = snprintf(event.comm.comm, sizeof(event.comm.comm),
			"krava") + 1;
	size = PERF_ALIGN(size, sizeof(u64));

	event.header.type = PERF_RECORD_COMM;
	event.header.size = sizeof(event.comm) -
			    (sizeof(event.comm.comm) - size);

	event.comm.pid = 1234;
	event.comm.tid = 4321;

	return &event;
}

static int process_comm(struct perf_tool *tool __maybe_unused,
			union perf_event *event,
			struct perf_sample *sample __maybe_unused,
			struct machine *machine __maybe_unused)
{
	pr_debug("event COMM pid %d, tid %d, comm '%s'\n",
		 event->comm.pid,
		 event->comm.tid,
		 event->comm.comm);

	TEST_ASSERT_VAL("wrong COMM pid\n",
		event->comm.pid == 1234);

	TEST_ASSERT_VAL("wrong COMM tid\n",
		event->comm.tid == 4321);

	TEST_ASSERT_VAL("wrong COMM comm\n",
		!strcmp(event->comm.comm, "krava"));

	events_comm++;
	return 0;
}

static union perf_event *get_event_EXIT(void)
{
	static union perf_event event;

	event.header.type = PERF_RECORD_EXIT;
	event.header.size = sizeof(event.fork);

	event.fork.pid = 2322;
	event.fork.tid = 2232;
	event.fork.ppid = 10;
	event.fork.ptid = 1;
	event.fork.time = 0xB16B00B5;

	return &event;
}

static int process_exit(struct perf_tool *tool __maybe_unused,
			union perf_event *event,
			struct perf_sample *sample __maybe_unused,
			struct machine *machine __maybe_unused)
{
	pr_debug("event EXIT pid %d, tid %d, ppid %d, ptid %d, time 0x%lx\n",
		 event->fork.pid, event->fork.tid,
		 event->fork.ppid, event->fork.ptid,
		 event->fork.time);

	TEST_ASSERT_VAL("wrong EXIT pid\n",
		event->fork.pid == 2322);

	TEST_ASSERT_VAL("wrong EXIT tid\n",
		event->fork.tid == 2232);

	TEST_ASSERT_VAL("wrong EXIT ppid\n",
		event->fork.ppid == 10);

	TEST_ASSERT_VAL("wrong EXIT ptid\n",
		event->fork.ptid == 1);

	TEST_ASSERT_VAL("wrong EXIT time\n",
		event->fork.time == 0xB16B00B5);

	events_exit++;
	return 0;
}

static union perf_event *get_event_FORK(void)
{
	static union perf_event event;

	event.header.type = PERF_RECORD_FORK;
	event.header.size = sizeof(event.fork);

	event.fork.pid  = 4321;
	event.fork.tid  = 1234;
	event.fork.ppid = 14321;
	event.fork.ptid = 11234;
	event.fork.time = 0xdeadbeef;

	return &event;
}

static int process_fork(struct perf_tool *tool __maybe_unused,
			union perf_event *event,
			struct perf_sample *sample __maybe_unused,
			struct machine *machine __maybe_unused)
{
	pr_debug("event FORK pid %d, tid %d, ppid %d, ptid %d, time 0x%lx\n",
		 event->fork.pid, event->fork.tid,
		 event->fork.ppid, event->fork.ptid,
		 event->fork.time);

	TEST_ASSERT_VAL("wrong FORK pid\n",
		event->fork.pid == 4321);

	TEST_ASSERT_VAL("wrong FORK tid\n",
		event->fork.tid == 1234);

	TEST_ASSERT_VAL("wrong FORK ppid\n",
		event->fork.ppid == 14321);

	TEST_ASSERT_VAL("wrong FORK ptid\n",
		event->fork.ptid == 11234);

	TEST_ASSERT_VAL("wrong FORK time\n",
		event->fork.time == 0xdeadbeef);

	events_fork++;
	return 0;
}

static union perf_event *get_event_THROTTLE(void)
{
	static union perf_event event;

	event.header.type = PERF_RECORD_THROTTLE;
	event.header.size = sizeof(event.throttle);

	event.throttle.time      = 0xdeadbeef;
	event.throttle.id        = 123;
	event.throttle.stream_id = 987;

	return &event;
}

static int process_throttle(struct perf_tool *tool __maybe_unused,
			    union perf_event *event,
			    struct perf_sample *sample __maybe_unused,
			    struct machine *machine __maybe_unused)
{
	pr_debug("event THROTTLE time 0x%lx, id 0x%lx, stream_id 0x%lx\n",
		 event->throttle.time, event->throttle.id,
		 event->throttle.stream_id);

	TEST_ASSERT_VAL("wrong THROTTLE time\n",
		event->throttle.time == 0xdeadbeef);

	TEST_ASSERT_VAL("wrong THROTTLE id\n",
		event->throttle.id == 123);

	TEST_ASSERT_VAL("wrong THROTTLE id\n",
		event->throttle.stream_id == 987);

	events_throttle++;
	return 0;
}

static union perf_event *get_event_UNTHROTTLE(void)
{
	static union perf_event event;

	event.header.type = PERF_RECORD_UNTHROTTLE;
	event.header.size = sizeof(event.throttle);

	event.throttle.time      = 0xdeadbeef;
	event.throttle.id        = 542318590;
	event.throttle.stream_id = 2341238951;

	return &event;
}

static int process_unthrottle(struct perf_tool *tool __maybe_unused,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	pr_debug("event UNTHROTTLE time 0x%lx, id 0x%lx, stream_id 0x%lx\n",
		 event->throttle.time, event->throttle.id,
		 event->throttle.stream_id);

	TEST_ASSERT_VAL("wrong UNTHROTTLE time\n",
		event->throttle.time == 0xdeadbeef);

	TEST_ASSERT_VAL("wrong UNTHROTTLE id\n",
		event->throttle.id == 542318590);

	TEST_ASSERT_VAL("wrong UNTHROTTLE id\n",
		event->throttle.stream_id == 2341238951);

	events_unthrottle++;
	return 0;
}

static union perf_event *get_event_SAMPLE(void)
{
	static unsigned char buf[4096];
	static union perf_event *event = (union perf_event *) buf;
	struct perf_sample sample = {
		.ip		= 0xaaa,
		.pid		= 0xedfa,
		.tid		= 0xabc,
		.time		= 123456789,
		.addr		= 0xabababab,
		.id		= 0xedf234abf,
		.stream_id	= 9273651,
		.period		= 0xdead,
		.weight		= 1,
		.cpu		= 1024,
		.data_src	= 3,
	};
	size_t sz;
	int ret;

	sz = perf_event__sample_event_size(&sample, sample_type, 0, 0);

	event->header.type = PERF_RECORD_SAMPLE;
	event->header.misc = 0;
	event->header.size = sz;

	ret = perf_event__synthesize_sample(event, sample_type, 0, 0,
					    &sample, false);
	return ret ? NULL : event;
}

static int process_sample(struct perf_tool *tool __maybe_unused,
			  union perf_event *event __maybe_unused,
			  struct perf_sample *sample,
			  struct perf_evsel *evsel __maybe_unused,
			  struct machine *machine __maybe_unused)
{
	pr_debug("event SAMPLE ip %lx, pid %x, tid %x, time %lx, "
		 "addr %lx, id %lx, stream_id %lx, period %lx, "
		 "weight %lx, cpu %x, data_src %lx\n",
		 sample->ip, sample->pid, sample->tid, sample->time,
		 sample->addr, sample->id, sample->stream_id,
		 sample->period, sample->weight, sample->cpu,
		 sample->data_src);

	TEST_ASSERT_VAL("wrong SAMPLE ip\n", sample->ip == 0xaaa);
	TEST_ASSERT_VAL("wrong SAMPLE pid\n", sample->pid == 0xedfa);
	TEST_ASSERT_VAL("wrong SAMPLE tid\n", sample->tid == 0xabc);
	TEST_ASSERT_VAL("wrong SAMPLE time\n", sample->time == 123456789);
	TEST_ASSERT_VAL("wrong SAMPLE addr\n", sample->addr == 0xabababab);
	TEST_ASSERT_VAL("wrong SAMPLE id\n", sample->id == 0xedf234abf);
	TEST_ASSERT_VAL("wrong SAMPLE stream_id\n",
			sample->stream_id == 9273651);
	TEST_ASSERT_VAL("wrong SAMPLE period\n", sample->period == 0xdead);
	TEST_ASSERT_VAL("wrong SAMPLE weight\n", sample->weight == 1);
	TEST_ASSERT_VAL("wrong SAMPLE cpu\n", sample->cpu == 1024);
	TEST_ASSERT_VAL("wrong SAMPLE data_src\n", sample->data_src == 3);

	events_sample++;
	return 0;
}

static int store_event(int fd, union perf_event *event, size_t *size)
{
	TEST_ASSERT_VAL("no event\n", event);
	*size += event->header.size;
	return write(fd, event, event->header.size) > 0 ? 0 : -1;
}

static int session_write(char *path)
{
	struct perf_data_file file = {
		.mode = PERF_DATA_MODE_WRITE,
		.path = path,
	};
	struct perf_session *session;
	struct perf_evlist *evlist;
	size_t size = 0;
	int feat;

	evlist = perf_evlist__new_default();
	TEST_ASSERT_VAL("failed to get evlist", evlist);

	perf_evlist__first(evlist)->attr.sample_type = sample_type;

	pr_debug("session writing start\n");

	session = perf_session__new(&file, false, NULL);
	TEST_ASSERT_VAL("failed to create session", session);

	session->evlist = evlist;

	for (feat = HEADER_FIRST_FEATURE; feat < HEADER_LAST_FEATURE; feat++)
		perf_header__set_feat(&session->header, feat);

	perf_header__clear_feat(&session->header, HEADER_BUILD_ID);
	perf_header__clear_feat(&session->header, HEADER_TRACING_DATA);
	perf_header__clear_feat(&session->header, HEADER_BRANCH_STACK);

	TEST_ASSERT_VAL("failed to write header",
		!perf_session__prepare_header(file.fd));

#define STORE_EVENTS(str, func, cnt)				\
do {								\
	int i;							\
	for (i = 0; i < cnt; i++) {				\
		TEST_ASSERT_VAL(str,				\
			!store_event(file.fd, func(), &size));	\
	}							\
} while (0)

	STORE_EVENTS("failed to store MMAP event",
		     get_event_MMAP, EVENTS_MMAP);

	STORE_EVENTS("failed to store LOST event",
		     get_event_LOST, EVENTS_LOST);

	STORE_EVENTS("failed to store COMM event",
		     get_event_COMM, EVENTS_COMM);

	STORE_EVENTS("failed to store EXIT event",
		     get_event_EXIT, EVENTS_EXIT);

	STORE_EVENTS("failed to store FORK event",
		     get_event_FORK, EVENTS_FORK);

	STORE_EVENTS("failed to store THROTTLE event",
		     get_event_THROTTLE, EVENTS_THROTTLE);

	STORE_EVENTS("failed to store UNTHROTTLE event",
		     get_event_UNTHROTTLE, EVENTS_UNTHROTTLE);

	STORE_EVENTS("failed to store SAMPLE event",
		     get_event_SAMPLE, EVENTS_SAMPLE);

#undef STORE_EVENTS

	session->header.data_size += size;

	TEST_ASSERT_VAL("failed to write header",
		!perf_session__write_header(session, evlist, file.fd));

	perf_session__delete(session);
	perf_evlist__delete(evlist);

	pr_debug("session writing stop\n");
	return 0;
}

static int __session_read(char *path)
{
	struct perf_session *session;
	struct perf_tool tool = {
		.mmap = process_mmap,
		.lost = process_lost,
		.comm = process_comm,
		.exit = process_exit,
		.fork = process_fork,
		.throttle = process_throttle,
		.unthrottle = process_unthrottle,
		.sample = process_sample,
	};
	struct perf_data_file file = {
		.mode = PERF_DATA_MODE_READ,
		.path = path,
	};

	pr_debug("session reading start\n");

	session = perf_session__new(&file, false, &tool);
	TEST_ASSERT_VAL("failed to create session", session);

	TEST_ASSERT_VAL("failed to process events",
		perf_session__process_events(session, &tool) == 0);

	perf_session__delete(session);

	pr_debug("session reading stop\n");
	return 0;
}

static int session_read(char *file)
{
	events_mmap = 0;
	events_lost = 0;
	events_comm = 0;
	events_exit = 0;
	events_fork = 0;
	events_throttle = 0;
	events_unthrottle = 0;
	events_sample = 0;

	if (__session_read(file))
		return -1;

	TEST_ASSERT_VAL("wrong MMAP events count", events_mmap == EVENTS_MMAP);
	TEST_ASSERT_VAL("wrong LOST events count", events_lost == EVENTS_LOST);
	TEST_ASSERT_VAL("wrong COMM events count", events_comm == EVENTS_COMM);
	TEST_ASSERT_VAL("wrong EXIT events count", events_exit == EVENTS_EXIT);
	TEST_ASSERT_VAL("wrong FORK events count", events_fork == EVENTS_FORK);
	TEST_ASSERT_VAL("wrong THROTTLE events count",
			events_throttle == EVENTS_THROTTLE);
	TEST_ASSERT_VAL("wrong UNTHROTTLE events count",
			events_unthrottle == EVENTS_UNTHROTTLE);
	TEST_ASSERT_VAL("wrong SAMPLE events count",
			events_sample == EVENTS_SAMPLE);
	return 0;
}

static int test_generated_data(void)
{
	char *file = get_file();
	int err = 0;

	TEST_ASSERT_VAL("failed to get temporary file", file);

	err = session_write(file);
	if (!err)
		err = session_read(file);

	unlink(file);
	return err;
}

static int file_write(char *file, char *data, ssize_t size)
{
	int fd = open(file, O_TRUNC|O_RDWR);
	int err = 0;

	if (size != write(fd, data, size))
		err = -1;

	close(fd);
	return err;
}

static int test_file_data(char *data, ssize_t size)
{
	char *file = get_file();
	int err = 0;

	TEST_ASSERT_VAL("failed to get temporary file", file);

	err = file_write(file, data, size);
	if (!err)
		err = session_read(file);

	unlink(file);
	return err;
}

int test__session_simple(void)
{
	int err;

	pr_debug("Testing generated data\n");
	err = test_generated_data();
	pr_debug("Testing v2 LE data\n");
	err |= test_file_data(data_file_v2_le, sizeof(data_file_v2_le));
	pr_debug("Testing v2 BE data\n");
	err |= test_file_data(data_file_v2_be, sizeof(data_file_v2_be));
	pr_debug("Testing v3 LE data\n");
	err |= test_file_data(data_file_v3_le, sizeof(data_file_v3_le));
	return err;
}

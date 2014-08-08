#include <linux/compiler.h>
#include <unistd.h>
#include "tests.h"
#include "poller.h"
#include "debug.h"

static int reader_hup_cnt;
static int reader_data_cnt;
static int writer_error_cnt;
static int wrong_cnt;

static int reader_hup_cb(struct poller *p __maybe_unused,
			 struct poller_item *item __maybe_unused)
{
	pr_debug("got reader_hup_cb\n");
	reader_hup_cnt++;
	return 0;
}

static int reader_data_cb(struct poller *p __maybe_unused,
			  struct poller_item *item __maybe_unused)
{
	pr_debug("got reader_data_cb\n");
	reader_data_cnt++;
	return 0;
}

static int writer_error_cb(struct poller *p __maybe_unused,
			   struct poller_item *item __maybe_unused)
{
	pr_debug("got writer_error_cb\n");
	writer_error_cnt++;
	return 0;
}

static int wrong_cb(struct poller *p __maybe_unused,
		    struct poller_item *item __maybe_unused)
{
	pr_debug("got wrong_cb\n");
	wrong_cnt++;
	return 0;
}

typedef int (*pipe_test_cb)(struct poller *p, int *pipefd);

static int test_hup(struct poller *poller, int *pipefd)
{
	/* We close the writer, we should get HUP on reader. */
	close(pipefd[1]);
	poller__poll(poller, -1);

	TEST_ASSERT_VAL("failed to get read hup", reader_hup_cnt == 1);
	TEST_ASSERT_VAL("failed, got wrong cnt", !wrong_cnt);
	return 0;
}

static int test_error(struct poller *poller, int *pipefd)
{
	/* We close the reader, we should get ERROR on writer. */
	close(pipefd[0]);
	poller__poll(poller, -1);

	TEST_ASSERT_VAL("failed to get write error", writer_error_cnt == 1);
	TEST_ASSERT_VAL("failed, got wrong cnt", !wrong_cnt);
	return 0;
}

static int test_data(struct poller *poller, int *pipefd)
{
	int data = 1;

	/* Writing data into writer, we should get data IN on reader. */
	TEST_ASSERT_VAL("failed to write data",
		write(pipefd[1], &data, sizeof(data)) == sizeof(data));

	poller__poll(poller, -1);

	TEST_ASSERT_VAL("failed to get reader data", reader_data_cnt == 1);
	TEST_ASSERT_VAL("failed, got wrong cnt", !wrong_cnt);
	return 0;
}

static int test_pipe(pipe_test_cb test)
{
	struct poller poller;
	struct poller_item reader = {
		.ops = {
			.data  = reader_data_cb,
			.error = wrong_cb,
			.hup   = reader_hup_cb,
		},
	};
	struct poller_item writer = {
		.ops = {
			.data  = wrong_cb,
			.error = writer_error_cb,
			.hup   = wrong_cb,
		},
	};
	int pipefd[2], err;

	TEST_ASSERT_VAL("failed to create pipe", !pipe(pipefd));

	poller__init(&poller);

	reader.fd = pipefd[0];
	TEST_ASSERT_VAL("failed to add reader", !poller__add(&poller, &reader));

	writer.fd = pipefd[1];
	TEST_ASSERT_VAL("failed to add writer", !poller__add(&poller, &writer));

	err = test(&poller, pipefd);

	poller__cleanup(&poller);
	close(pipefd[0]);
	close(pipefd[1]);
	return err;
}

int test__poller(void)
{
	TEST_ASSERT_VAL("failed to test HUP  ", !test_pipe(test_hup));
	TEST_ASSERT_VAL("failed to test ERROR", !test_pipe(test_error));
	TEST_ASSERT_VAL("failed to test DATA ", !test_pipe(test_data));
	return 0;
}

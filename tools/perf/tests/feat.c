#include <libaudit.h>
#include "debug.h"
#include "tests.h"
#include "feat.h"

int test__feat(void)
{
	TEST_ASSERT_VAL("failed to call", PF(TEST, getpid)());
	return 0;
}

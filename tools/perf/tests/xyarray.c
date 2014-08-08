#include "tests.h"
#include "xyarray.h"
#include "debug.h"

struct krava {
	int a;
};

#define X 100
#define Y 100

int test__xyarray(void)
{
	struct xyarray *a;
	struct krava *k;
	int x, y;

	a = xyarray__new(X, Y, sizeof(struct krava));
	TEST_ASSERT_VAL("failed to allocate xyarray", a);

	for (x = 0; x < X; x++) {
		for (y = 0; y < Y; y++) {
			k = xyarray__entry(a, x, y);
			k->a = x * X + y;
		}
	}

	y = 0;
	xyarray__for_each(a, k)
		TEST_ASSERT_VAL("wrong array value", k->a == y++);

	return 0;
}

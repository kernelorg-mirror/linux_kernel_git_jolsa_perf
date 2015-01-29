#include <lzma.h>

int main(void)
{
	lzma_stream strm = LZMA_STREAM_INIT;

	lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
	return 0;
}

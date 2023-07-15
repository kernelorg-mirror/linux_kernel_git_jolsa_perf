
extern int krava(void);

__attribute__((noinline)) int krava(void)
{
	return 1;
}

int main(void)
{
	while(krava());
}

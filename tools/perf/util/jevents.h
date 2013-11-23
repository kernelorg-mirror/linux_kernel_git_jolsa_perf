int json_events(const char *fn,
		int (*func)(void *data, char *name, char *event, char *desc),
		void *data);

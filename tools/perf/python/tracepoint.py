#! /usr/bin/python
# -*- python -*-
# -*- coding: utf-8 -*-

import perf

class tracepoint(perf.evsel):
    def __init__(self, sys, name):
        config = perf.tracepoint(sys, name)
        perf.evsel.__init__(self,
                            type   = perf.TYPE_TRACEPOINT,
                            config = config,
                            freq = 0, sample_period = 1, wakeup_events = 1,
                            sample_type = perf.SAMPLE_PERIOD | perf.SAMPLE_TID | perf.SAMPLE_CPU | perf.SAMPLE_RAW)

def main():
	cpus = perf.cpu_map()
	threads = perf.thread_map(-1)
	tp = tracepoint("sched", "sched_switch")

	evlist = perf.evlist(cpus, threads)
	evlist.add(tp)
	evlist.open()
	evlist.mmap()
	while True:
		evlist.poll(timeout = -1)
		for cpu in cpus:
			event = evlist.read_on_cpu(cpu)
			if not event:
				continue
			print "cpu: %2d, pid: %4d, tid: %4d - prev pid %d, prev comm %s" % (event.sample_cpu,
				 event.sample_pid,
				 event.sample_tid,
				 event.prev_pid,
                                 event.prev_comm),
			print event

if __name__ == '__main__':
    main()

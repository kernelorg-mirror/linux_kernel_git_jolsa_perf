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
    tp      = tracepoint("sched", "sched_switch")
    cpus    = perf.cpu_map()
    threads = perf.thread_map(-1)

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

            if not isinstance(event, perf.sample_event):
                continue

            print "prev_comm=%s prev_pid=%d prev_prio=%d prev_state=%s ==> next_comm=%s next_pid=%d next_prio=%d" % (
                   event.prev_comm,
                   event.prev_pid,
                   event.prev_prio,
                   event.prev_state,
                   event.next_comm,
                   event.next_pid,
                   event.next_prio)

if __name__ == '__main__':
    main()

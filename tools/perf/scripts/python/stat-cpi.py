#!/bin/python

data    = {}
times   = []
threads = []
cpus    = []

def get_key(time, event, cpu, thread):
    return "%d-%s-%d-%d" % (time, event, cpu, thread)

def store_key(time, cpu, thread):
    if (time not in times):
        times.append(time)

    if (cpu not in cpus):
        cpus.append(cpu)

    if (thread not in threads):
        threads.append(thread)

def store(time, event, cpu, thread, val, ena, run):
    #print "event %s cpu %d, thread %d, time %d, val %d, ena %d, run %d" % \
    #      (event, cpu, thread, time, val, ena, run)

    store_key(time, cpu, thread)
    key = get_key(time, event, cpu, thread)
    data[key] = [ val, ena, run]

def get(time, event, cpu, thread):
    key = get_key(time, event, cpu, thread)
    return data[key][0]

def stat__cycles(cpu, thread, time, val, ena, run):
    store(time, "cycles", cpu, thread, val, ena, run);

def stat__instructions(cpu, thread, time, val, ena, run):
    store(time, "instructions", cpu, thread, val, ena, run);

def trace_end():
    for time in times:
        for cpu in cpus:
            for thread in threads:
                cyc = get(time, "cycles", cpu, thread)
                ins = get(time, "instructions", cpu, thread)
                cpi = cyc/float(ins)
                print "time %.9f, cpu %d, thread %d -> cpi %f" % (time/(float(1000000000)), cpu, thread, cpi)

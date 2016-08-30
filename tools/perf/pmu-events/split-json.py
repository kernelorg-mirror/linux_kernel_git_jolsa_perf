#!/bin/python

import sys
import re
import os

idx_from = 0
idx_to   = 0
path     = sys.argv[1];
regex    = ""
regexb   = r"^},*"

def get_idx():
    global idx_from
    global idx_to
    global regex

    #print regex

    found = 0
    idx   = 1
    for line in open(path):
        line = line.strip(' \t\n\r')

        #print "get: %s" % line

        if (line == "{"):
            idx_from = idx

        if re.search(regexb, line):
            idx_to = idx
            if found == 1:
                break;

        if re.search(regex, line):
            found = 1

        idx += 1

    if found <> 1:
        idx_from = 0;

    #print "found %d, from %d, to %d" % (found, idx_from, idx_to)

def get_file(topic):
    global regex

    print "Getting %s" % (topic)

    regex = r"\"Topic\":\s\"%s\"," % topic
    out   = "%s.json" % topic
    out   = out.replace(" ", "-");

    os.system("echo '[' > %s" % out)

    while True:
        global idx_from
        global idx_to

        idx_from = 0
        idx_to   = 0

        get_idx()

        if idx_from == 0:
            break;

        #raw_input("Press Enter to continue...")

        run = "sed -n '%d,%dp' '%s' >> %s" % (idx_from, idx_to, path, out)
        os.system(run)

        run = "sed '%d,%dd' -i '%s'" % (idx_from, idx_to, path)
        os.system(run)

    os.system("echo ']' >> %s" % out)
    os.system("sed '/%s/d' -i %s" % (regex, out))


get_file("Cache")
get_file("Floating point")
get_file("Memory")
get_file("Other")
get_file("Pipeline")
get_file("Virtual Memory")
get_file("Frontend")

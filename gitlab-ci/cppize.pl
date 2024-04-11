#!/usr/bin/perl -pi
use strict;
use warnings;
# rewrites the Makefile to be suitable for compiling as C++

use vars qw($first); # declare a global variable outside the main loop

print "CC = g++\n\n" unless $first++; # write this only once

s/^CC\s*=.*$//; # remove the original compiler selection if there is any
s/-std=\w+/-std=gnu++03/; # must have a good language version
s/ -Wstrict-prototypes//; # this warning is not applicable to C++


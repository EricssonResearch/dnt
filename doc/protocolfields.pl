#!/usr/bin/perl
# build md from the protocol definitions
use strict;
use warnings;

print <<EOF;

# Protocol fields

These are the supported fields of the known protocols.

This list was generated with $0.
EOF

my $protoname;

while (<>) {
    if ($protoname) {
        if (/^};/) {
            undef $protoname
        } else {
            /"(\w+)"/ and print "* `$1`";
            /\/\/(.*)/ and print "$1";
            print "\n";
        }
    } else {
        if (/^static const struct ProtocolField (\w+)_fields/) {
            $protoname = $1;
            print "\n## $protoname\n\n"
        }
    }
}

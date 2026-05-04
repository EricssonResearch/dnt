#!/usr/bin/perl
# build md from the protocol definitions
#   usage: protocolfields.pl protocol.c > protocols.md
use strict;
use warnings;

print <<EOF;

# Protocol fields

These are the supported fields of the known protocols.

This list was generated with $0.
EOF

my $protoname;
my %headers;
my %protocols;

while (<>) {
    if ($protoname) {
        if (/^};/) {
            undef $protoname
        } else {
            my ($name) = /"(\w+)"/;
            next unless defined $name;
            my ($comment) = /\/\/(.*)/;
            $comment = '' unless defined $comment;
            push @{$headers{$protoname}}, "* `$name` $comment\n";
        }
    } else {
        if (/^static const struct ProtocolField (\w+)_fields/) {
            $protoname = $1;
        }
        if (/^    \{"(\w+)", (\w+)_fields/) {
            $protocols{$1} = $2;
        }
    }
}

for my $proto (sort keys %protocols) {
    my $fields = $headers{$protocols{$proto}};
    next unless defined $fields;

    print "\n## $proto\n\n";
    print for @$fields;
}

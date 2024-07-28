#!/usr/bin/perl
# generate input data for test_conf_utils.c
use strict;
use warnings;

srand(44);

for my $bitcount (1..64) {
    for my $offs (0..80-$bitcount) {
        # note: prepare_constant_number() reduces the offset to be <8
        my $off = $offs % 8;

        # all zeros, all ones
        my @zeros = (zeros($off), zeros($bitcount), zeros(80-$off-$bitcount));
        my @ones  = (zeros($off),  ones($bitcount), zeros(80-$off-$bitcount));
        my $zero = bits2hex(@zeros);
        my $one = bits2hex(@ones);
        my $zero_num = 0;
        my $one_num = (1<<$bitcount) - 1;
        print "{{$zero},${zero_num}lu,$offs,$bitcount},\n";
        print "{{$one},${one_num}lu,$offs,$bitcount},\n";

        # random bits
        for my $i (0..3) {
            my @rndbits = random($bitcount);
            my @rand = (zeros($off), @rndbits, zeros(80-$off-$bitcount));
            my $rand = bits2hex(@rand);
            # convert @rndbits to a number
            my $num = 0;
            for my $n (@rndbits) {
                $num = ($num << 1) + $n;
            }
            print "{{$rand},${num}lu,$offs,$bitcount},\n";
        }
    }
}

sub zeros {
    my $n = shift;
    return (0) x $n
}

sub ones {
    my $n = shift;
    return (1) x $n
}

sub random {
    my $n = shift;
    return map { int rand(2) } (1) x $n
}

# converts an array of 80 bits into a string of 10 hex numbers
#   (0,1,0,1,0,1, ...) -> '0x55,0xde ...'
sub bits2hex {
    my @bits = @_;

    my @bytes = unpack("A8" x 10, join '', @bits); # form strings of 8 bits
    my $bin = pack("B8" x 10, @bytes); # convert to binary data
    my @hex = unpack("H2" x 10, $bin); # convert the data to hex
    return join ',', map { "0x$_" } @hex
}

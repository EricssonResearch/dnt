#!/usr/bin/perl
# generate input data for test_header_compare.c
use strict;
use warnings;

srand(44);

for my $bitcount (1..32) {
    for my $a_offs (0..64-$bitcount) {
        my $bo = $a_offs % 8;
        for (my $b_offs=$bo; $b_offs+$bitcount<=64; $b_offs+=8) {

            # all zero, all one bit patterns
            for my $aout (\&zeros, \&ones) { # a bits outside
                for my $bout (\&zeros, \&ones) { # b bits outside
                    for my $ins (
                        [\&zeros, \&zeros, 'true'],
                        [\&ones, \&ones, 'true'],
                        [\&zeros, \&ones, 'false'],
                        [\&ones, \&zeros, 'false']) { # the bits inside
                        my @a = ($aout->($a_offs), $ins->[0]($bitcount), $aout->(64-$a_offs-$bitcount));
                        my @b = ($bout->($b_offs), $ins->[1]($bitcount), $bout->(64-$b_offs-$bitcount));
                        my $a = bits2hex(@a);
                        my $b = bits2hex(@b);

                        print "{{$a},{$b},$a_offs,$b_offs,$bitcount,$ins->[2]},\n"
                    }
                }
            }

            # patterns where 1 bit is different
            for my $aout (\&zeros, \&ones) { # a bits outside
                for my $bout (\&zeros, \&ones) { # b bits outside
                    for my $ins (\&zeros, \&ones) { # the bits inside
                        for my $i (0..$bitcount-1) {
                            my @a = ($aout->($a_offs), invert1bit($i, $ins->($bitcount)), $aout->(64-$a_offs-$bitcount));
                            my @b = ($bout->($b_offs), $ins->($bitcount), $bout->(64-$b_offs-$bitcount));
                            my $a = bits2hex(@a);
                            my $b = bits2hex(@b);

                            print "{{$a},{$b},$a_offs,$b_offs,$bitcount,false},\n"
                        }
                        for my $i (0..$bitcount-1) {
                            my @a = ($aout->($a_offs), $ins->($bitcount), $aout->(64-$a_offs-$bitcount));
                            my @b = ($bout->($b_offs), invert1bit($i, $ins->($bitcount)), $bout->(64-$b_offs-$bitcount));
                            my $a = bits2hex(@a);
                            my $b = bits2hex(@b);

                            print "{{$a},{$b},$a_offs,$b_offs,$bitcount,false},\n"
                        }
                    }
                }
            }

            #TODO random bit patterns?
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

# converts an array of 64 bits into a string of 8 hex numbers
#   (0,1,0,1,0,1, ...) -> '0x55,0xde ...'
sub bits2hex {
    my @bits = @_;

    my @bytes = unpack("A8" x 8, join '', @bits); # form strings of 8 bits
    my $bin = pack("B8" x 8, @bytes); # convert to binary data
    my @hex = unpack("H2" x 8, $bin); # convert the data to hex
    return join ',', map { "0x$_" } @hex
}

sub invert1bit {
    my $i = shift;
    my @bits = @_;

    $bits[$i] = 1 - $bits[$i];
    return @bits
}

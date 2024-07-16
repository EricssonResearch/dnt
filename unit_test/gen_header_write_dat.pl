#!/usr/bin/perl
# generate input data for test_header_write.c
use strict;
use warnings;

srand(44);

for my $bitcount (1..32) {
    for my $src_offs (0..64-$bitcount) {
        my $bo = $src_offs % 8;
        for (my $dst_offs=$bo; $dst_offs+$bitcount<=64; $dst_offs+=8) {

            # all zero, all one bit patterns
            for my $sout (\&zeros, \&ones) { # source bits outside
                for my $dout (\&zeros, \&ones) { # destination bits outside
                    for my $ins (
                        [\&zeros, \&zeros],
                        [\&ones, \&ones],
                        [\&zeros, \&ones],
                        [\&ones, \&zeros]) { # how bits change inside
                        my @src = ($sout->($src_offs), $ins->[1]($bitcount), $sout->(64-$src_offs-$bitcount));
                        my @dst = ($dout->($dst_offs), $ins->[0]($bitcount), $dout->(64-$dst_offs-$bitcount));
                        my @xpc = ($dout->($dst_offs), $ins->[1]($bitcount), $dout->(64-$dst_offs-$bitcount));
                        my $src = bits2hex(@src);
                        my $dst = bits2hex(@dst);
                        my $xpc = bits2hex(@xpc);

                        print "{{$src},{$dst},{$xpc},$src_offs,$dst_offs,$bitcount},\n"
                    }
                }
            }

            # random bit patterns
            for my $i (0..3) {
                my @s_pre  = random($src_offs);
                my @s_in   = random($bitcount);
                my @s_post = random(64-$src_offs-$bitcount);
                my @d_pre  = random($dst_offs);
                my @d_in   = random($bitcount);
                my @d_post = random(64-$dst_offs-$bitcount);

                my @src = (@s_pre, @s_in, @s_post);
                my @dst = (@d_pre, @d_in, @d_post);
                my @xpc = (@d_pre, @s_in, @d_post);
                my $src = bits2hex(@src);
                my $dst = bits2hex(@dst);
                my $xpc = bits2hex(@xpc);

                print "{{$src},{$dst},{$xpc},$src_offs,$dst_offs,$bitcount},\n"
            }
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

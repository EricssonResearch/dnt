#!/usr/bin/perl
# do some checks on the CI config:
#   verify that all unit tests are run
#   TODO other verifications?
use strict;
use warnings;

# this must be installed from CPAN
use YAML::Tiny;

#use Data::Dumper;

my $yamlfname = '.gitlab-ci.yml';
my $cmakefname = 'unit_test/CMakeLists.txt';

my $yaml = YAML::Tiny->read($yamlfname);
die "Can't read '$yamlfname'\n" unless $yaml;

my $ci = $yaml->[0]; # get the first document (we only have one...)

verify_unit_tests($ci);


sub verify_unit_tests {
    my $ci = shift;
    my @ci_unit_test_jobs = grep /^unit_test\./, keys %$ci;

    # verify that the unit test jobs have the same name as the tests
    for my $ut (@ci_unit_test_jobs) {
        my ($ut_name) = $ut =~ /^unit_test\.(\w+)/;
        die "Unit test job name '$ut' is invalid\n" unless defined $ut_name;
        my $script = $ci->{$ut}{script}[0];
        die "Unit test job '$ut' has no script\n" unless defined $script;
        my ($script_name) = $script =~ /unit_test\/build\/test_(\w+)/;
        die "Unit test script of job '$ut' is invalid\n" unless defined $script_name;
        die "Unit test job '$ut' script doesn't match the test\n" unless $ut_name eq $script_name;
    }

    # check for duplicate jobs
    my %ci_unit_test_names;
    $ci_unit_test_names{$_}++ for @ci_unit_test_jobs;
    for my $k (keys %ci_unit_test_names) {
        die "Unit test runs $ci_unit_test_names{$k} times\n" if $ci_unit_test_names{$k} > 1
    }

    # see that all unit tests are in the CI
    open (my $CM, "<", $cmakefname) or die "Can't open '$cmakefname': $!\n";

    my @test_exe;
    while (my $l = <$CM>) {
        chomp $l;
        next unless $l =~ /^add_test_/;
        my ($exe_name) = $l =~ /add_test_\w+\(test_(\w+)/;
        die "Unit test executable name is invalid in '$l'\n" unless defined $exe_name;
        push @test_exe, $exe_name
    }

    for my $exe (@test_exe) {
        die "CI doesn't run unit test '$exe'\n" unless exists $ci_unit_test_names{"unit_test.$exe"}
    }
}


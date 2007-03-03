#!/usr/bin/perl

use strict;
use warnings;

use Test::More tests => 19;

use lib 't/lib';

BEGIN {
    use_ok('Test::SVN', qw(create_and_load_repo));
    use_ok('SVN::Client');
}

my $repo_path = create_and_load_repo('t/repo.dump');
my $repo_url  = "file://$repo_path";

my $ctx = SVN::Client->new();
isa_ok($ctx, 'SVN::Client');

my $called = 0;
my @path = ('', 'A', 'iota');
my $list_func = sub {
    my($path, $dirent, $lock, $abs_path, $pool) = @_;

    is($path, $path[$called++], "\$path is correct - $path");
    isa_ok($dirent, '_p_svn_dirent_t');
    is($lock, undef, '$lock is undefined');
    is($abs_path, '/', "\$abs_path is correct - $abs_path");
    isa_ok($pool, '_p_apr_pool_t');
};

$ctx->list({
    path_or_url   => $repo_url,
    peg_revision  => 1,
    revision      => 1,
    recurse       => 0,
    dirent_fields => $SVN::Core::SVN_DIRENT_ALL,
    fetch_locks   => 1,
    list_func     => $list_func,
});

is($called, 3, '$list_func called 3 times');


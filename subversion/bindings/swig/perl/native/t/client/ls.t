#!/usr/bin/perl

use strict;
use warnings;

use Test::More tests => 79;

use lib 't/lib';

BEGIN {
    use_ok('Test::SVN', qw(create_and_load_repo));
    use_ok('SVN::Client');
}

my $repo_path = create_and_load_repo('t/repo.dump');
my $repo_url  = "file://$repo_path";

my $ctx = SVN::Client->new();
isa_ok($ctx, 'SVN::Client');

my $dirents;

diag 'Testing r0';
foreach my $args (([$repo_url, 0, 1],
		   [ { path_or_url => $repo_url, revision => 0, recurse => 1 } ])) {

    $dirents = $ctx->ls(@{ $args });
    isa_ok($dirents, 'HASH');
    is_deeply($dirents, { }, 'r0 is empty');
}

diag 'Testing r1';
foreach my $args (([$repo_url, 1, 1],
		   [ { path_or_url => $repo_url, revision => 1, recurse => 1 } ])) {

    $dirents = $ctx->ls(@{ $args });
    is(scalar keys %{ $dirents }, 20, '20 dirents listed');

    foreach my $dirent (keys %{ $dirents }) {
	isa_ok($dirents->{$dirent}, '_p_svn_dirent_t');
    }

    is($dirents->{'A'}->size(), 0, '/A size is zero');
    is($dirents->{'A'}->kind(), $SVN::Node::dir, '/A is a directory');
    is($dirents->{'A'}->has_props(), 0, '/A has no properties');
    my $iota = $dirents->{'iota'};
    is($iota->kind(), $SVN::Node::file, '/iota is a file');
    is($iota->size(), 25, '/iota is 25 bytes');
    is($iota->has_props(), 0, '/iota has no properties');
    is($iota->created_rev(), 1, '/iota was changed in this rev');
    is($iota->time, 1171532569978151, '/iota has correct change time');
    is($iota->last_author(), 'nik', '/iota has correct author');
}

foreach my $args ((["$repo_url/A", 1, 0],
		   [ { path_or_url => "$repo_url/A", revision => 1, recurse => 0 } ],
	           [ { path_or_url => "$repo_url/A", revision => 1 } ], )) {

    $dirents = $ctx->ls(@{ $args });
    foreach my $dirent (qw(B C D)) {
	is($dirents->{$dirent}->kind(), $SVN::Node::dir, "/A/$dirent is a directory");
    }
    is($dirents->{'mu'}->kind(), $SVN::Node::file, '/A/mu is a file');
}

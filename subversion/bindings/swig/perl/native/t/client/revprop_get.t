#!/usr/bin/perl

use strict;
use warnings;

use Test::More tests => 15;

use lib 't/lib';

BEGIN {
    use_ok('Test::SVN', qw(create_and_load_repo));
    use_ok('SVN::Client');
}

my $repo_path = create_and_load_repo('t/repo.dump');
my $repo_url  = "file://$repo_path";

my $ctx = SVN::Client->new();
isa_ok($ctx, 'SVN::Client');

# XXX this is only going to work while HEAD == r2, and committed by 'nik'
my $tester = sub {
    my($rpg_val, $rpg_rev, $name) = @_;
    diag $name;
    is($rpg_val, 'nik', 'svn:author set to "nik"');
    is($rpg_rev, 2, 'revprop_get() returns correct revision');
};

diag 'Running revprop_get() on the repo HEAD';

$tester->($ctx->revprop_get('svn:author', $repo_url, 'HEAD'), 'pos/man');
$tester->($ctx->revprop_get('svn:author', $repo_url), 'pos/opt');
$tester->($ctx->revprop_get({
    propname => 'svn:author',
    url      => $repo_url,
    revision => 'HEAD'
}), 'nam/man');
$tester->($ctx->revprop_get({
    propname => 'svn:author',
    url      => $repo_url,
}), 'nam/opt');

diag 'Running revprop_get() on r1';

$tester = sub {
    my($rpg_val, $rpg_rev, $name) = @_;
    diag $name;
    is($rpg_val, 'nik', 'svn:author set to "nik"');
    is($rpg_rev, 1, 'revprop_get() returns correct revision');
};

$tester->($ctx->revprop_get('svn:author', $repo_url, 1), 'pos/man');
$tester->($ctx->revprop_get({
    propname => 'svn:author',
    url      => $repo_url,
    revision => 1
}), 'nam/man');

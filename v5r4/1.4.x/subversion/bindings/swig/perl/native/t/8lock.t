#!/usr/bin/perl -w
use Test::More tests => 8;
use strict;
no warnings 'once'; # shut up about variables that are only used once.

require SVN::Core;
require SVN::Repos;
require SVN::Fs;
use File::Path qw(rmtree);
use File::Temp qw(tempdir);

my $repospath = tempdir('svn-perl-test-XXXXXX', TMPDIR => 1, CLEANUP => 1);

my $repos;

ok($repos = SVN::Repos::create("$repospath", undef, undef, undef, undef),
   "create repository at $repospath");

my $fs = $repos->fs;

my $acc = SVN::Fs::create_access('foo');
is ($acc->get_username, 'foo');
$fs->set_access($acc);

my $txn = $fs->begin_txn($fs->youngest_rev);
$txn->root->make_file('testfile');
{
my $stream = $txn->root->apply_text('testfile', undef);
print $stream 'orz';
}
$txn->commit;

$fs->lock ('/testfile', 'hate software', 'we hate software', 0, 0, $fs->youngest_rev, 0);

ok(my $lock = $fs->get_lock('/testfile'));
is ($lock->token, 'hate software');
is ($lock->owner, 'foo');

my $acc = SVN::Fs::create_access('fnord');
is ($acc->get_username, 'fnord');
$fs->set_access($acc);

eval {
$fs->lock ('/testfile', 'hate software', 'we hate software', 0, 0, $fs->youngest_rev, 0);
};

like($@, qr/already locked/);

eval {
$fs->unlock('/testfile', 'software', 0)
};
like($@, qr/no such lock/);

$fs->unlock('/testfile', 'software', 1);

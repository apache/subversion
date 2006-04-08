#!/usr/bin/perl -w

use Test::More tests => 6;
use File::Temp qw(tempdir);
use File::Path qw(rmtree);
use strict;

use SVN::Core;
use SVN::Repos;
use SVN::Ra;
use SVN::Fs;
use SVN::Delta;

my $repospath = tempdir('svn-perl-test-XXXXXX', TMPDIR => 1, CLEANUP => 1);

my $repos;

ok($repos = SVN::Repos::create("$repospath", undef, undef, undef, undef),
   "create repository at $repospath");

my $fs = $repos->fs;
my $txn = $fs->begin_txn($fs->youngest_rev);
$txn->root->make_dir('trunk');
$txn->commit;

my $uri = $repospath;
$uri =~ s{^|\\}{/}g if $^O eq 'MSWin32';
$uri = "file://$uri";
my $ra = SVN::Ra->new( url => $uri);
isa_ok ($ra, 'SVN::Ra');
is ($ra->get_uuid, $fs->get_uuid);
is ($ra->get_latest_revnum, 1);

isa_ok ($ra->rev_proplist (1), 'HASH');
#is ($ra->get_latest_revnum, 0);

my $reporter = $ra->do_update (1, '', 1, SVN::Delta::Editor->new);
isa_ok ($reporter, 'SVN::Ra::Reporter');
$reporter->abort_report;

END {
diag "cleanup";
rmtree($repospath);
}

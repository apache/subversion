#!/usr/bin/perl -w

use Test::More tests => 16;
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

cmp_ok($fs->youngest_rev, '==', 0,
       "new repository start with rev 0");

is($fs->path, "$repospath/db", '$fs->path()');
is(SVN::Fs::type($fs->path), 'fsfs', 'SVN::Fs::type()');

my $txn = $fs->begin_txn($fs->youngest_rev);

my $txns = $fs->list_transactions;
ok(eq_array($fs->list_transactions, [$txn->name]), 'list transaction');

isa_ok($txn->root, '_p_svn_fs_root_t', '$txn->root()');
is($txn->root->txn_name, $txn->name, '$txn->root->txn_name()');
is($fs->revision_root($fs->youngest_rev)->txn_name, undef);

$txn->root->make_dir('trunk');

my $path = 'trunk/filea';
my $text = "this is just a test\n";
$txn->root->make_file('trunk/filea');
{
my $stream = $txn->root->apply_text('trunk/filea', undef);
print $stream $text;
close $stream;
}
$txn->commit;

cmp_ok($fs->youngest_rev, '==', 1, 'revision increased');

my $root = $fs->revision_root ($fs->youngest_rev);

cmp_ok($root->check_path($path), '==', $SVN::Node::file);
ok (!$root->is_dir($path));
ok ($root->is_file($path));
{
my $stream = $root->file_contents ($path);
local $/;
is(<$stream>, $text, 'content verified');
is($root->file_md5_checksum ($path), 'dd2314129f81675e95b940ff94ddc935',
   'md5 verified');
}

cmp_ok( $root->file_length ($path), '==', length($text) );

is ($fs->revision_prop(1, 'not:exists'), undef, 'nonexisting property');

END {
diag "cleanup";
rmtree($repospath);
}

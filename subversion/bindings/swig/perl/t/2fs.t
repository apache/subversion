#!/usr/bin/perl -w

use Test::More qw(no_plan);
use strict;

require SVN::Core;
require SVN::Repos;
require SVN::Fs;

my $repospath = "/tmp/svn-$$";

my $repos;

ok($repos = SVN::Repos::create("$repospath", undef, undef, undef, undef),
   "create repository at $repospath");

my $fs = $repos->fs;

cmp_ok($fs->youngest_rev, '==', 0,
       "new repository start with rev 0");

my $txn = $fs->begin_txn($fs->youngest_rev);

my $txns = $fs->list_transactions;
ok(eq_array($fs->list_transactions, [$txn->name]), 'list transaction');

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
}
ok (eq_array(SVN::Repos::revisions_changed ($fs, 'trunk/filea', 0, 1, 0), [1]),
    'revisions_changed');

is ($fs->revision_prop(1, 'not:exists'), undef, 'nonexisting property');

END {
diag "cleanup";
`rm -rf $repospath`;
}

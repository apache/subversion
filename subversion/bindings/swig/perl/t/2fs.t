#!/usr/bin/perl

use Test::More qw(no_plan);
use strict;
BEGIN {
use_ok 'SVN::Core';
use_ok 'SVN::Repos';
use_ok 'SVN::Fs';
}

my $repospath = "/tmp/svn-$$";

my $repos;

ok($repos = SVN::Repos::create("$repospath", undef, undef, undef, undef),
   "create repository at $repospath");

my $fs = $repos->fs;

cmp_ok($fs->youngest_rev, '==', 0,
       "new repository start with rev 0");

my $txn = SVN::Fs::begin_txn($fs, $fs->youngest_rev);

my $txns = $fs->list_transactions;
ok(eq_array($fs->list_transactions, [$txn->name]), 'list transaction');

SVN::Fs::make_dir($txn->root, 'trunk');

my $path = 'trunk/filea';
my $text = "this is just a test\n";
SVN::Fs::make_file($txn->root, 'trunk/filea');
my $stream = SVN::Fs::apply_text($txn->root, 'trunk/filea', undef);
$stream->write($text);
$stream->close;

SVN::Fs::commit_txn($txn);

cmp_ok($fs->youngest_rev, '==', 1, 'revision increased');

my $root = $fs->revision_root ($fs->youngest_rev);

cmp_ok(SVN::Fs::check_path($root, $path), '==', $SVN::Core::node_file);

my $filelen = SVN::Fs::file_length($root, $path);
my $stream = SVN::Fs::file_contents($root, $path);
is($stream->read($filelen), $text, 'content verified');

ok (eq_array(SVN::Repos::revisions_changed ($fs, 'trunk/filea', 0, 1, 0), [1]),
    'revisions_changed');

is ($fs->revision_prop(1, 'not:exists'), undef, 'nonexisting property');

END {
diag "cleanup";
`rm -rf $repospath`;
}

#!/usr/bin/perl

use Test::More qw(no_plan);
use strict;

require SVN::Core;
require SVN::Repos;
require SVN::Fs;
require SVN::Delta;

my $repospath = "/tmp/svn-$$";

my $repos;

ok($repos = SVN::Repos::create("$repospath", undef, undef, undef, undef),
   "create repository at $repospath");

my $fs = $repos->fs;

sub committed {
    diag "committed ".join(',',@_);
}

my $pool = SVN::Core::pool_create(undef);

my $editor = SVN::Delta::Editor->
    new(SVN::Repos::get_commit_editor($repos, "file://$repospath",
				      '/', 'root', 'FOO', \&committed));

my $rootbaton = $editor->open_root(0, $pool);

my $dirbaton = $editor->add_directory ('trunk', $rootbaton, undef, 0, $pool);

my $fbaton = $editor->add_file ('trunk/filea', $dirbaton,
			       undef, -1, $pool);

my $ret = $editor->apply_textdelta ($fbaton, undef, $pool);

SVN::_Delta::svn_txdelta_send_string("FILEA CONTENT",
                                    @$ret, $pool);

#SVN::TxDelta::send_string("FILEA CONTENT", @$ret, $pool);

$editor->close_edit($pool);

cmp_ok($fs->youngest_rev, '==', 1);
{
$editor = SVN::Delta::Editor->
    new (SVN::Repos::get_commit_editor($repos, "file://$repospath",
				       '/', 'root', 'FOO', \&committed));
my $rootbaton = $editor->open_root(1, $pool);

my $dirbaton = $editor->add_directory ('tags', $rootbaton, undef, 1, $pool);
my $subdirbaton = $editor->add_directory ('tags/foo', $dirbaton, 
					  "file://$repospath/trunk", 1, $pool);

$editor->close_edit($pool);
}
cmp_ok($fs->youngest_rev, '==', 2);

{
$editor = SVN::Delta::Editor->
    new (SVN::Repos::get_commit_editor($repos, "file://$repospath",
				       '/', 'root', 'FOO', \&committed));

my $rootbaton = $editor->open_root(2, $pool);
$editor->delete_entry('tags', 2, $rootbaton, $pool);

$editor->close_edit($pool);
}

cmp_ok($fs->youngest_rev, '==', 3);

END {
diag "cleanup";
print `svn cat file://$repospath/trunk/filea`;
print `svn log -v file://$repospath`;
`rm -rf $repospath`;
}

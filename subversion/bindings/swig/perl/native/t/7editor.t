#!/usr/bin/perl -w
package MyEditor;
our @ISA = ('SVN::Delta::Editor');

sub add_file {
    die;
}

package main;
use Test::More tests => 1;
use File::Temp qw(tempdir);
use File::Path qw(rmtree);
use strict;

require SVN::Core;
require SVN::Repos;
require SVN::Fs;
require SVN::Delta;

my $repospath = tempdir('svn-perl-test-XXXXXX', TMPDIR => 1, CLEANUP => 1);
my $repos = SVN::Repos::create("$repospath", undef, undef, undef, undef);
my $fs = $repos->fs;

sub committed {
    diag "committed ".join(',',@_);
}

my $editor = SVN::Delta::Editor->
    new(SVN::Repos::get_commit_editor($repos, "file://$repospath",
				      '/', 'root', 'FOO', \&committed));
my $rootbaton = $editor->open_root(0);
my $dirbaton = $editor->add_directory ('trunk', $rootbaton, undef, 0);
my $fbaton = $editor->add_file ('trunk/filea', $dirbaton, undef, -1);
my $ret = $editor->apply_textdelta ($fbaton, undef);
SVN::TxDelta::send_string("FILEA CONTENT", @$ret);
$editor->close_edit();
eval {
my $pool = SVN::Pool->new_default;
SVN::Repos::dir_delta ($fs->revision_root (0), '/', '',
		       $fs->revision_root (1), '/',
		       MyEditor->new (crap => bless {}, 'something'),
		       undef, 1, 1, 0, 0);
};
ok ($main::something_destroyed, 'editor');

package something;

sub DESTROY {
    $main::something_destroyed++;
}


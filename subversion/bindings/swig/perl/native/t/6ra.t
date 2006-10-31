#!/usr/bin/perl -w

use Test::More tests => 7;
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

my $ed = MockEditor->new;
$ra->replay(1, 0, 1, $ed);
is($ed->{trunk}{type}, 'dir', "replay: got trunk");

END {
diag "cleanup";
rmtree($repospath);
}


package MockEditor;

sub new { bless {}, shift }

sub set_target_revision {
    my ($self, $revnum) = @_;
    $self->{_target_revnum} = $revnum;
}

sub delete_entry {
    my ($self, $path) = @_;
    die "delete_entry called";
}

sub add_directory {
    my ($self, $path, $baton) = @_;
    return $self->{$path} = { type => 'dir' };
}

sub open_root {
    my ($self, $base_revision, $dir_pool) = @_;
    $self->{_base_revnum} = $base_revision;
    return $self->{_root} = { type => 'root' };
}

sub open_directory {
    my ($self, $path) = @_;
    die "open_directory on file" unless $self->{$path}{type} eq 'dir';
    return $self->{$path};
}

sub open_file {
    my ($self, $path) = @_;
    die "open_file on directory" unless $self->{$path}{type} eq 'file';
    return $self->{$path};
}

sub change_dir_prop {
    my ($self, $baton, $name, $value) = @_;
    $baton->{props}{$name} = $value;
}

sub change_file_prop {
    my ($self, $baton, $name, $value) = @_;
    $baton->{props}{$name} = $value;
}

sub absent_directory {
    my ($self, $path) = @_;
    die "absent_directory called";
}

sub absent_file {
    my ($self, $path) = @_;
    die "absent_file called";
}

sub close_directory {
    my ($self, $baton) = @_;
}

sub close_file {
    my ($self, $baton) = @_;
}

sub add_file {
    my ($self, $path, $baton) = @_;
    return $self->{$path} = { type => 'file' };
}

sub apply_textdelta {
    my ($self, $baton, $base_checksum, $pool) = @_;
    my $data = $baton->{data} = \'';
    open my $out_fh, '>', $data
        or die "error opening in-memory file to store Subversion update: $!";
    open my $in_fh, '<', \''
        or die "error opening in-memory file for delta source: $!";
    return [ SVN::TxDelta::apply($in_fh, $out_fh, undef, "$baton", $pool) ];
}

sub close_edit {
    my ($self, $pool) = @_;
}

sub abort_edit {
    my ($self, $pool) = @_;
    die "abort_edit called";
}

use strict;
use warnings;

package SVN::Repos;
use SVN::Base qw(Repos svn_repos_);

=head1 NAME

SVN::Repos - Subversion repository functions

=head1 SYNOPSIS

    use SVN::Core;
    use SVN::Repos;
    use SVN::Fs;

    my $repos = SVN::Repos::open('/path/to/repos');
    print $repos->fs()->youngest_rev;

=head1 DESCRIPTION

SVN::Repos wraps the object-oriented C<svn_repos_t> functions, providing
access to a Subversion repository on the local filesystem.

=cut

# Build up a list of methods as we go through the file.  Add each method
# to @methods, then document it.  The full list of methods is then
# instantiated at the bottom of this file.
#
# This should make it easier to keep the documentation and list of methods
# in sync.

my @methods = ();		# List of functions to wrap

=head2 CONSTRUCTORS

=over

=item SVN::Repos::open($path)

This function opens an existing repository, and returns an
C<SVN::Repos> object.

=item create($path, undef, undef, $config, $fs_config)

This function creates a new repository, and returns an C<SVN::Repos>
object.

=back

=head2 METHODS

=over

=cut

push @methods, qw(fs);

=item $repos-E<gt>fs()

Returns the C<SVN::Fs> object for this repository.

=cut

push @methods, qw(get_logs);

=item $repos-E<gt>get_logs([$path, ...], $start, $end, $discover_changed_paths, $strict_node_history, $receiver)

Iterates over all the revisions that affect the list of paths passed
as the first parameter, starting at $start, and ending at $end.

$receiver is called for each change.  The arguments to $receiver are:

=over

=item $self

The C<SVN::Repos> object.

=item $paths

C<undef> if $discover_changed_paths is false.  Otherwise, contains a hash
of paths that have changed in this revision.

=item $rev

The revision this change occured in.

=item $date

The date and time the revision occured.

=item $msg

The log message associated with this revision.

=item $pool

An C<SVN::Pool> object which may be used in the function.

=back

If $strict_node_history is true then copies will not be traversed.

=back

=cut

=head2 ADDITIONAL METHODS

The following methods work, but are not currently documented in this
file.  Please consult the svn_repos.h section in the Subversion API
for more details.

=over

=item $repos-E<gt>get_commit_editor(...)

=item $repos-E<gt>get_commit_editor2(...)

=item $repos-E<gt>path(...)

=item $repos-E<gt>db_env(...)

=item $repos-E<gt>lock_dir(...)

=item $repos-E<gt>db_lockfile(...)

=item $repos-E<gt>hook_dir(...)

=item $repos-E<gt>start_commit_hook(...)

=item $repos-E<gt>pre_commit_hook(...)

=item $repos-E<gt>post_commit_hook(...)

=item $repos-E<gt>pre_revprop_change(...)

=item $repos-E<gt>post_revprop_change(...)

=item $repos-E<gt>dated_revision(...)

=item $repos-E<gt>fs_commit_txn(...)

=item $repos-E<gt>fs_being_txn_for_commit(...)

=item $repos-E<gt>fs_being_txn_for_update(...)

=item $repos-E<gt>fs_change_rev_prop(...)

=item $repos-E<gt>node_editor(...)

=item $repos-E<gt>dump_fs(...)

=item $repos-E<gt>load_fs(...)

=item $repos-E<gt>get_fs_build_parser(...)

=back

=cut

push @methods, qw(get_commit_editor get_commit_editor2
		  path db_env lock_dir
		  db_lockfile hook_dir start_commit_hook
		  pre_commit_hook post_commit_hook
		  pre_revprop_change_hook post_revprop_change_hook
		  dated_revision fs_commit_txn fs_begin_txn_for_commit
		  fs_begin_txn_for_update fs_change_rev_prop
		  node_editor dump_fs load_fs get_fs_build_parser);

{
    no strict 'refs';
    for (@methods) {
	*{"_p_svn_repos_t::$_"} = *{$_};
    }
}

=head1 AUTHORS

Chia-liang Kao E<lt>clkao@clkao.orgE<gt>

=head1 COPYRIGHT

Copyright (c) 2003-2006 CollabNet.  All rights reserved.

This software is licensed as described in the file COPYING, which you
should have received as part of this distribution.  The terms are also
available at http://subversion.tigris.org/license-1.html.  If newer
versions of this license are posted there, you may use a newer version
instead, at your option.

This software consists of voluntary contributions made by many
individuals.  For exact contribution history, see the revision history
and logs, available at http://subversion.tigris.org/.

=cut

1;

package Test::SVN;

use strict;
use warnings;

use Exporter qw(import);
our @EXPORT_OK = qw(create_repo load_repo create_and_load_repo);

use File::Temp qw(tempdir);
use File::Spec;

my $SVNADMIN;

# Work out where svnadmin(1) is.
sub _find_svnadmin {
    return $SVNADMIN if defined $SVNADMIN;

    my $path = shift;
    $path = '.' unless defined $path;

    return if $path eq File::Spec->rootdir(); # Hit the root

    # Look for 'svnadmin/svnadmin' as a subdirectory of the path we're
    # checking.  If we find it, return that path
    my $poss_path = File::Spec->catfile($path, 'svnadmin', 'svnadmin');
    if(-x $poss_path) {
	$SVNADMIN = $poss_path;
	return $SVNADMIN;
    }

    # Try again, but a directory higher
    return _find_svnadmin(File::Spec->catfile($path, File::Spec->updir()));
}

# Provide useful functions for testing the Subversion Perl bindings

=head2 create_repo

  my $path     = create_repo();
  my $repo_url = "file://$path";

Creates a temporary directory and makes a repository in that
directory.

Returns the path in which the repository was created.  The temporary
directory is automatically removed when the test program ends.

=cut

sub create_repo {
    my $svnadmin = _find_svnadmin();
    die "Can't find svnadmin(1)\n" unless defined $svnadmin;

    my $dir = tempdir(CLEANUP => 1);

    system $svnadmin, 'create', $dir;

    return $dir;
}

=head2 load_repo

  load_repo($repo_path, $dump_file);

Loads the contents of $dump_file in to the repository at $repo_path.

=cut

sub load_repo {
    my $repo      = shift;
    my $dump_file = shift;

    my $svnadmin = _find_svnadmin();

    open(my $dump_fh, '<', $dump_file) or die $!;
    open(my $repo_fh, '|-', $svnadmin, 'load', '--quiet', $repo) or die $!;

    local $_;
    while(<$dump_fh>) {
	print $repo_fh $_;
    }

    close($dump_fh);
    close($repo_fh);
}

=head2 create_and_load_repo

  my $repo_path = create_and_load_repo($dump_file);

Creates a new empty repository (using C<create_repo()>), and loads
$dump_file in to the repository.

Returns the path to the newly created repository.

=cut

sub create_and_load_repo {
    my $dump_file = shift;

    my $repo = create_repo();
    load_repo($repo, $dump_file);

    return $repo;
}

1;

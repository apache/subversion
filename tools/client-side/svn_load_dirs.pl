#!/usr/bin/perl -w

use strict;
use Carp;
use Cwd;
use File::Copy   2.03;
use File::Find;
use File::Path   1.0404;
use File::Temp   0.12   qw(tempdir);
use Getopt::Long 2.25;
use URI          1.17;

my $VERSION = 0.01;

# These are configurable parameters.

# The regular expression to use to match portions of the directory
# names to determine the tag name.
my $opt_tag_regex;

GetOptions('tag_regex=s' => \$opt_tag_regex) or
  &usage;

# Two arguments are needed at minimum, the svn URL and one directory
# to load.
&usage("$0: too few arguments") if @ARGV < 2;

# Convert the string URL into a URI object.
my $svn_url = shift;
$svn_url    =~ s#/*$##;
my $svn_uri = URI->new($svn_url);

# Compile the regular expression and surround it with ()'s to save the
# results of the match.
my $opt_tag_re;
if ($opt_tag_regex) {
  $opt_tag_re = qr/($opt_tag_regex)/;
}

# The remaining command line arguments should be directories.
my @load_dirs = @ARGV;
my @load_tags;

# Check that all of the directories exist and print the load tags for
# review.
{
  my $one_load_tag;
  foreach my $dir (@load_dirs) {
    unless (-e $dir) {
      die "$0: directory `$dir' does not exist.\n";
    }
    unless (-d $dir) {
      die "$0: directory `$dir' is not a directory.\n";
    }

    my $load_tag;
    if ($opt_tag_re) {
      ($load_tag) = $dir =~ $opt_tag_re;
      if (defined $load_tag) {
        print "Directory $dir will be tagged as $load_tag\n";
        $one_load_tag = 1;
      }
    }
    push(@load_tags, $load_tag);
  }

  if ($one_load_tag) {
    print "Please examine identified tags.  Are they acceptable? (y/n) ";
    my $line;
    do {
      $line   = <>;
      $line ||= '';
    } until $line =~ /[yn]/i;
    if ($line =~ /n/i) {
      exit 0;
    }
  }
}

# If there is at least one tag, check that the number of tags and
# directories match.
if (@load_tags and @load_tags != @load_dirs) {
  die "$0: number of tags does not match number of directories.\n";
}

# Check that there are no duplicate directories and tag numbers.
{
  my %dirs;
  foreach my $dir (@load_dirs) {
    if ($dirs{$dir}) {
      die "$0: directory $dir is listed more than once on command line.\n";
    }
    $dirs{$dir} = 1;
  }

  my %tags;
  foreach my $tag (@load_tags) {
    if ($tags{$tag}) {
      die "$0: directory tag $tag occurs more than once.\n";
    }
    $tags{$tag} = 1;
  }
}

# Get the current working directory.
my $orig_cwd = cwd;

# The first step is to determine the root of the svn repository.  Do
# this with the svn log command.  Take the svn_url hostname and port
# as the initial url and and append to it successive portions of the
# final path until svn log succeeds.
my $svn_root_uri;
my $svn_repos_path_segment;
my @svn_path_segments;
{
  my $r = $svn_uri->clone;
  my @path_segments = grep { length($_) } $r->path_segments;
  unshift(@path_segments, '');
  $r->path('');
  my @r_path_segments;

  while (@path_segments) {
    $svn_repos_path_segment = shift @path_segments;
    push(@r_path_segments, $svn_repos_path_segment);
    $r->path_segments(@r_path_segments);
    if (safe_read_from_pipe('svn', 'log', "$r") == 0) {
      $svn_root_uri      = $r;
      @svn_path_segments = @path_segments;
      last;
    }
  }
}

unless ($svn_root_uri) {
  die "$0: cannot determine root svn URL.\n";
}

print "Determined that the svn root URL is $svn_root_uri\n";

# Create a temporary directory for svn to work in.
my $temp_template = '/tmp/svn_log_dirs_XXXXXXXXXX';
my $temp_dir      = tempdir($temp_template);

# Create an object that when DESTROY'ed will delete the temporary
# directory.  The CLEANUP flag to tempdir should do this, but they
# call rmtree with 1 as the last argument which takes extra security
# measures that do not clean up the .svn directories.
my $temp_dir_cleanup = Temp::Delete->new;

chdir($temp_dir) or
  die "$0: cannot chdir `$temp_dir': $!\n";

# Now check out the entire svn repository into a fixed directory name.
my $checkout_dir_name = 'ZZZ';
print "Checking out $svn_root_uri into $temp_dir\n";
{
  my @command = ('svn', 'co', "$svn_root_uri", '-d', $checkout_dir_name);
  my ($status, @output) = safe_read_from_pipe(@command);
  if ($status) {
    die join("\n", "$0: @command failed with this output:", @output), "\n";
  }
}

# Change into the top level directory of the repository.
chdir($checkout_dir_name) or
  die "$0: cannot chdir `$checkout_dir_name': $!\n";

# Record this location because the script will come back here.
my $repos_top_dir = cwd;

# Check if the standard svn directories exist for the project.  If
# not, ask if they should be created.
{
  my $project_path   = join('/', @svn_path_segments);
  $project_path    ||= '.';
  my @add_project_path;
  foreach my $path ('', 'branches', 'tags', 'trunk') {
    my $p = $path ? "$project_path/$path" : $project_path;
    unless (-d $p) {
      push(@add_project_path, $p);
    }
  }

  if (@add_project_path) {
    print "The following directories do not exist for this project:\n";
    foreach my $path (@add_project_path) {
      print "  $path\n";
    }
    print "You must add them now to load the directories.  Continue (y/n)? ";
    my $line;
    do {
      $line = <>;
      $line ||= '';
    } until $line =~ /[yn]/i;
    if ($line =~ /n/i) {
      exit 0;
    }

    my $message = "Creating standard subversion directories for new project.\n";

    foreach my $path (@add_project_path) {
      mkdir($path) or
        die "$0: cannot mkdir `$path': $!\n";
      if ($svn_repos_path_segment) {
        $message .= "\n* $svn_repos_path_segment/$path: New directory.\n";
      } else {
        $message .= "\n* $path: New directory.\n";
      }
    }
    my @command = ('svn', 'add', @add_project_path);
    my ($status, @output) = safe_read_from_pipe(@command);
    if ($status) {
      die join("\n", "$0: @command failed with this output:", @output), "\n";
    }

    @command = ('svn', 'commit', '-m', $message);
    ($status, @output) = safe_read_from_pipe(@command);
    if ($status) {
      die join("\n", "$0: @command failed with this output:", @output), "\n";
    }
  } else {
    print "Standard svn directories exist for $svn_uri\n";
  }
}

# Now go through each source directory and copy each file from the
# source directory to the target directory.  For new target files, add
# them to svn.  For files that no longer exist, delete them.
my $repos_trunk_dir    = join('/',
                              $repos_top_dir,
                              @svn_path_segments,
                              'trunk');
my $repos_tag_dir      = join('/',
                              $repos_top_dir,
                              @svn_path_segments,
                              'tags');
my $repos_project_path = join('/',
                              $checkout_dir_name,
                              @svn_path_segments,
                              'trunk');

while (@load_dirs) {
  my $load_dir = shift @load_dirs;
  my $load_tag = shift @load_tags;

  if (defined $load_tag) {
    print "Loading $load_dir and will save in tag $load_tag.\n";
  } else {
    print "Loading $load_dir.\n";
  }

  my @add_files;
  my @del_files;

  # First get a list of all the files and directories in the target
  # repository.  This is used to see what files should be deleted from
  # here.
  chdir($repos_trunk_dir) or
    die "$0: cannot chdir `$repos_trunk_dir': $!\n";

  my %existing_files;
  my $wanted = sub {
    s#^\./##;
    return if $_ eq '.';
    $existing_files{$_} = 1;
  };
  find({no_chdir   => 1,
        preprocess => sub { grep { $_ !~ /\.svn$/ } @_ },
        wanted     => $wanted
       }, '.');

  # Change to the original directory so that the specified directories
  # can be properly located.
  chdir($orig_cwd) or
    die "$0: cannot chdir `$orig_cwd': $!\n";

  # Now go into the directory to load.
  chdir($load_dir) or
    die "$0: cannot chdir `$load_dir': $!\n";

  $wanted = sub {
    s#^\./##;
    return if $_ eq '.';

    my $source_path = $_;
    my $dest_path   = "$repos_trunk_dir/$_";

    my $source_type = &file_type($source_path);
    my $dest_type   = &file_type($dest_path);

    # Fail if the destination type exists but is of a different type
    # of file than the source type.
    if ($dest_type ne '0' and $source_type ne $dest_type) {
      die "$0: does not handle chaning source and destination type for ",
          "$source_path.\n";
    }

    if (delete $existing_files{$source_path}) {
      print "U   $source_path\n";
    } else {
      print "A   $source_path\n";
      push(@add_files, $source_path);
    }

    # Now copy the new file over the svn repository.
    if ($source_type eq 'd') {
      if ($dest_type eq '0') {
        mkdir($dest_path) or
          die "$0: cannot mkdir `$dest_path': $!\n";
      }
    } elsif ($source_type eq 'f') {
      copy($source_path, $dest_path) or
        die "$0: copy `$source_path' to `$dest_path': $!\n";
    } else {
      die "$0: does not handle copying files of type `$source_type'.\n";
    }
  };

  find({no_chdir   => 1,
        preprocess => sub { sort { $b cmp $a } @_ },
        wanted     => $wanted
       }, '.');

  # To delete the files, deeper files and directories must be deleted
  # first.  Because we have a hash keyed by remaining files and
  # directories to be deleted, instead of trying to figure out which
  # directories and files are contained in other directories, just
  # reverse sort by the path length and then alphabetically.
  @del_files = sort {length($b) <=> length($a) || $a cmp $b }
               keys %existing_files;
  foreach my $file (@del_files) {
    print "D   $file\n";
  }

  # Set up the names for the path to the trunk and tag.
  my @svn_uri_path_segments = $svn_uri->path_segments;
  my $trunk_uri = $svn_uri->clone;
  $trunk_uri->path_segments(@svn_uri_path_segments, 'trunk');
  my $trunk_path = $trunk_uri->path;

  # Now change back to the trunk directory and run the svn commands.
  chdir($repos_trunk_dir) or
    die "$0: cannot chdir `$repos_trunk_dir': $!\n";

  if (@add_files) {
    my @command = ('svn', 'add', @add_files);
    my ($status, @output) = safe_read_from_pipe(@command);
    if ($status) {
      die join("\n", "$0: @command failed with this output:", @output), "\n";
    }
  }
  if (@del_files) {
    my @command = ('svn', 'rm', @del_files);
    my ($status, @output) = safe_read_from_pipe(@command);
    if ($status) {
      die join("\n", "$0: @command failed with this output:", @output), "\n";
    }
  }

  # Now do the commit.
  my @command = ('svn', 'commit',
                 '-m', "Load $load_dir into $trunk_path.");
  my ($status, @output) = safe_read_from_pipe(@command);
  if ($status) {
    die join("\n", "$0: @command failed with this output:", @output), "\n";
  }

  # Now remove any files to be deleted in the repository.
  if (@del_files) {
    rmtree(\@del_files, 1, 0);
  }

  # Now make the tag by doing a copy in the svn repository itself.
  if (defined $load_tag) {
    my $tag_uri   = $svn_uri->clone;
    $tag_uri->path_segments(@svn_uri_path_segments, 'tags', $load_tag);
    my $tag_path   = $tag_uri->path;

    @command = ('svn', 'cp', '-m', "Tag $trunk_path as $tag_path.",
                "$trunk_uri", "$tag_uri");
    ($status, @output) = safe_read_from_pipe(@command);
    if ($status) {
      die join("\n", "$0: @command failed with this output:", @output), "\n";
    }
  }

  # Finally, go to the top of the svn checked out tree and do an
  # update to pick up the new tag in the working copy.
  chdir($repos_top_dir) or
    die "$0: cannot chdir `$repos_top_dir': $!\n";

  @command = qw(svn update);
  ($status, @output) = safe_read_from_pipe(@command);
  if ($status) {
    die join("\n", "$0: @command failed with this output:", @output), "\n";
  }

  # Now run a recursive diff between the original source directory and
  # the tag for a consistency check.
  if (defined $load_tag) {
    chdir($orig_cwd) or
      die "$0: cannot chdir `$orig_cwd': $!\n";
    @command = ('diff',
                '-x', '.svn',
                '-r', $load_dir, "$repos_tag_dir/$load_tag");
    ($status, @output) = safe_read_from_pipe(@command);
    if ($status) {
      die join("\n", "$0: @command failed with this output:", @output), "\n";
    }
  }
}

exit 0;

sub usage {
  warn "@_\n" if @_;
  die "usage: $0 [-t regex] svn_url dir_v1 [dir_v2 [dir_v3 [..]]]\n";
}

sub file_type {
  lstat(shift) or return '0';
  -b _ and return 'b';
  -c _ and return 'c';
  -d _ and return 'd';
  -f _ and return 'f';
  -l _ and return 'l';
  -p _ and return 'p';
  -S _ and return 'S';
  return '?';
}

sub safe_read_from_pipe {
  unless (@_) {
    croak "$0: safe_read_from_pipe passed no arguments.\n";
  }
  print "Running @_\n";
  my $pid = open(SAFE_READ, '-|');
  unless (defined $pid) {
    die "$0: cannot fork: $!\n";
  }
  unless ($pid) {
    open(STDERR, ">&STDOUT") or
      die "$0: cannot dup STDOUT: $!\n";
    exec(@_) or
      die "$0: cannot exec `@_': $!\n";
  }
  my @output;
  while (<SAFE_READ>) {
    chomp;
    push(@output, $_);
  }
  my $result = close(SAFE_READ);
  my $exit   = $? >> 8;
  my $signal = $? & 127;
  my $cd     = $? & 128 ? "with core dump" : "";
  if ($signal or $cd) {
    warn "$0: pipe from `@_' failed $cd: \$?=$exit signal=$signal\n";
  }
  if (wantarray) {
    return ($exit, @output);
  } else {
    return $exit;
  }
}

# This package exists just to delete the temporary directory.
package Temp::Delete;

sub new {
  bless {}, shift;
}

sub DESTROY {
  print "Cleaning up $temp_dir\n";
  File::Path::rmtree([$temp_dir], 0, 0);
}

# local variables:
# eval: (load-file "../dev/svn-dev.el")
# end:

#!/usr/bin/perl -wT

# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$

$| = 1;

use strict;
use CGI       2.89;
use CGI::Carp 1.24 qw(fatalsToBrowser carpout);
use vars           qw($query);

# Get a CGI object now and send the HTTP headers out immediately so
# that anything else printed will appear in the output, including
# compile errors.
BEGIN {
  $query = CGI->new;
  print $query->header('text/plain');
  carpout(\*STDOUT);
}

# Protect the PATH environmental variable for safe system calls.
$ENV{PATH} = '/usr/bin:/bin';

# Configuration settings.

# The location of the svn program.
my $svn = '/opt/i386-linux/subversion/bin/svn';

# The location of the svn_load_dirs.pl script.
my $svn_load_dirs = '/export/home2/svn/bin/svn_load_dirs.pl';

# The source directory.
my $source_dirname = '/export/home2/svn/public_html/www-devel/webdav';

# The target directory.
my $target_dirname = '/export/home1/apache/htdocs/www';

# The URL for the Subversion repository.
my $repos_base_uri = 'file:///export/home2/svn/repos-www/trunk';

# Verbosity level.
my $opt_verbose = 1;
my @opt_verbose = $opt_verbose ? (qw(-v)) : ();

# Use this version of die instead of Perl's die so that messages are
# sent to STDOUT instead of STDERR so that the browser can see them.
# Otherwise, messages would be sent to Apache's error_log.
sub my_die ($@)
{
  print "@_\n" if @_;
  exit 1;
}

# For permissions information, print my actual and effective UID and
# GID.
if ($opt_verbose)
  {
    my $real_uid      = getpwuid($<) || $<;
    my $effective_uid = getpwuid($>) || $>;
    my $real_gid      = getgrgid($() || $(;
    my $effective_gid = getgrgid($)) || $);

    print "My real uid is $real_uid and my effective uid is $effective_uid.\n";
    print "My real gid is $real_gid and my effective gid is $effective_gid.\n";
  }

# Check the configuration settings.
-e $source_dirname
  or my_die "$0: source directory `$source_dirname' does not exist.\n";
-d _
  or my_die "$0: source directory `$source_dirname' is not a directory.\n";
-e $target_dirname
  or my_die "$0: target directory `$target_dirname' does not exist.\n";
-d _
  or my_die "$0: target directory `$target_dirname' is not a directory.\n";

# Since the path to svn and svn_load_dirs.pl depends upon the local
# installation preferences, check that the required programs exist to
# insure that the administrator has set up the script properly.
{
  my $ok = 1;
  foreach my $program ($svn, $svn_load_dirs)
    {
      if (-e $program)
        {
          unless (-x $program)
            {
              print "$0: required program `$program' is not executable, ",
                    "edit $0.\n";
              $ok = 0;
            }
        }
      else
        {
          print "$0: required program `$program' does not exist, edit $0.\n";
          $ok = 0;
        }
    }
  exit 1 unless $ok;
}

# Check that the svn base URL works by running svn log on it.
&read_from_process($svn, 'log', $repos_base_uri);

# Determine the authentication username for commit privileges.
# Untaint the REMOTE_USER environmental variable.
my $username;
if (defined $ENV{REMOTE_USER})
  {
    ($username) = $ENV{REMOTE_USER} =~ m/(\w+)/;
    unless (defined $username and length $username)
      {
        my_die "$0: REMOTE_USER set to `$ENV{REMOTE_USER}' but no valid ",
               "string extracted from it.\n";
      }
  }
else
  {
    my_die "$0: the REMOTE_USER environmental variable is not set.\n";
  }

if ($opt_verbose)
  {
    print "I am logged in as `$username'.\n";
  }

# Load the source directory into Subversion.
print "Now syncing Subversion repository with source directory.\n\n";
my_system($svn_load_dirs,
          @opt_verbose,
          '-no_user_input',
          '-svn_username', $username,
          '-p', '/opt/i386-linux/installed/svn_load_dirs_property_table.cfg',
          $repos_base_uri,
          '.',
          $source_dirname) == 0
  or my_die "$0: system failed.  Quitting.\n";

print "\nNow syncing target directory with Subversion repository.\n\n";

chdir $target_dirname
  or my_die "$0: chdir `$target_dirname' failed: $!\n";
my_system($svn, 'update', '.') == 0
  or my_die "$0: system failed.  Quitting.\n";

print "\nTarget directory sucessfully updated to mirror source directory.\n";

exit 0;

# Start a child process safely without using /bin/sh.
sub safe_read_from_pipe
{
  unless (@_)
    {
      croak "$0: safe_read_from_pipe passed no arguments.\n";
    }

  if ($opt_verbose)
    {
      print "Running @_\n";
    }

  my $pid = open(SAFE_READ, '-|');
  unless (defined $pid)
    {
      my_die "$0: cannot fork: $!\n";
    }
  unless ($pid)
    {
      open(STDERR, ">&STDOUT")
        or my_die "$0: cannot dup STDOUT: $!\n";
      exec(@_)
        or my_die "$0: cannot exec `@_': $!\n";
    }
  my @output;
  while (<SAFE_READ>)
    {
      chomp;
      push(@output, $_);
    }
  close(SAFE_READ);
  my $result = $?;
  my $exit   = $result >> 8;
  my $signal = $result & 127;
  my $cd     = $result & 128 ? "with core dump" : "";
  if ($signal or $cd)
    {
      print "$0: pipe from `@_' failed $cd: exit=$exit signal=$signal\n";
    }
  if (wantarray)
    {
      return ($result, @output);
    }
  else
    {
      return $result;
    }
}

# Use safe_read_from_pipe to start a child process safely and exit the
# script if the child failed for whatever reason.
sub read_from_process
  {
  unless (@_)
    {
      croak "$0: read_from_process passed no arguments.\n";
    }
  my ($status, @output) = &safe_read_from_pipe(@_);
  if ($status)
    {
      my_die "$0: @_ failed with this output:\n", join("\n", @output), "\n";
    }
  else
    {
      return @output;
    }
}

# Run system() and print warnings on system's return values.
sub my_system
{
  unless (@_)
    {
      confess "$0: my_system passed incorrect number of arguments.\n";
    }

  if ($opt_verbose)
    {
      print "Running @_\n";
    }

  my $result = system(@_);
  if ($result == -1)
    {
      print "$0: system(@_) call itself failed: $!\n";
    }
  elsif ($result)
    {
      my $exit_value  = $? >> 8;
      my $signal_num  = $? & 127;
      my $dumped_core = $? & 128;

      my $message     = "$0: system(@_) exited with status $exit_value";
      if ($signal_num)
        {
          $message     .= " caught signal $signal_num";
        }
      if ($dumped_core)
        {
          $message     .= " and dumped core";
        }
      print "$message\n";
    }

  $result;
}

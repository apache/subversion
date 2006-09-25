#!/usr/bin/perl -w

# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$

use strict;
use Carp;
use Getopt::Long 2.25;

# Process the command line options.

# Print the log message along with the modifications made in a
# particular revision.
my $opt_print_log_message;

GetOptions('log' => \$opt_print_log_message)
  or &usage;

&usage("$0: too many arguments") if @ARGV > 1;

# If there is no file or directory specified on the command line, use
# the current working directory as a default path.
my $file_or_dir = @ARGV ? shift : '.';

unless (-e $file_or_dir)
  {
    die "$0: file or directory `$file_or_dir' does not exist.\n";
  }

# Get the entire log for this file or directory.  Parse the log into
# two separate lists.  The first is a list of the revision numbers
# when this file or directory was modified.  The second is a hash of
# log messages for each revision.
my @revisions;
my %log_messages;
{
  my $current_revision;
  foreach my $log_line (read_from_process('svn', 'log', $file_or_dir))
    {
      # Ignore any of the lines containing only -'s.
      next if $log_line =~ /^-+$/;

      if (my ($r) = $log_line =~ /^r(\d+)/)
        {
          $current_revision                = $r;
          $log_messages{$current_revision} = "";
          push(@revisions, $r);
        }

      if (defined $current_revision)
        {
          $log_messages{$current_revision} .= "$log_line\n";
        }
    }
}

# Run all the diffs.
while (@revisions > 1)
  {
    my $new_rev = shift @revisions;
    my $old_rev = $revisions[0];

    &print_revision($new_rev);

    my @diff = read_from_process('svn', 'diff',
                                 "-r$old_rev:$new_rev", $file_or_dir);

    if ($opt_print_log_message)
      {
        print $log_messages{$new_rev};
      }
    print join("\n", @diff, "\n");
  }

# Print the log message for the last revision.  There is no diff for
# this revision, because according to svn log, the file or directory
# did not exist previously.
{
  my $last_revision = shift @revisions;
  if ($opt_print_log_message)
    {
      &print_revision($last_revision);
      print $log_messages{$last_revision};
    }
}

exit 0;

sub usage
{
  warn "@_\n" if @_;
  die "usage: $0 [options] [file_or_dir]\n",
      "Valid options:\n",
      "  -l [--log] : also show the log messages\n";
}

sub print_revision
{
  my $revision = shift;

  print "\n\n\nRevision $revision\n";
}

# Start a child process safely without using /bin/sh.
sub safe_read_from_pipe
{
  unless (@_)
    {
      croak "$0: safe_read_from_pipe passed no arguments.\n";
    }
  print "Running @_\n";
  my $pid = open(SAFE_READ, '-|');
  unless (defined $pid)
    {
      die "$0: cannot fork: $!\n";
    }
  unless ($pid)
    {
      open(STDERR, ">&STDOUT")
        or die "$0: cannot dup STDOUT: $!\n";
      exec(@_)
        or die "$0: cannot exec `@_': $!\n";
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
      warn "$0: pipe from `@_' failed $cd: exit=$exit signal=$signal\n";
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
      die "$0: @_ failed with this output:\n", join("\n", @output), "\n";
    }
  else
    {
      return @output;
    }
}

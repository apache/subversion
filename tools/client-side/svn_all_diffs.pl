#!/usr/bin/perl -w

# $HeadURL$
# $LastChangedDate$
# $LastChangedBy$
# $LastChangedRevision$

use strict;
use Carp;

&usage("$0: too few arguments") if @ARGV < 1;
&usage("$0: too many arguments") if @ARGV > 1;

my $file_or_dir = shift;

unless (-e $file_or_dir)
  {
    die "$0: file or directory `$file_or_dir' does not exist.\n";
  }

# Get all the revisions that this file or directory changed.  This
# gets the revisions in latest to oldest order.
my @revs = map { m/^rev (\d+)/ } read_from_process('svn', 'log', $file_or_dir);

# Run all the diffs.
while (@revs > 1) {
  my $new_rev = shift @revs;
  my $old_rev = $revs[0];
  print join("\n", read_from_process('svn', 'diff',
                                     "-r$old_rev:$new_rev", $file_or_dir),
                   "\n");  
}

exit 0;

sub usage
{
  warn "@_\n" if @_;
  die "usage: $0 file_or_dir\n";
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

#!/usr/bin/perl -w

# ====================================================================
# Show log messages matching a certain pattern.  Usage:
#
#    search-svnlog.pl [-f LOGFILE] REGEXP
#
# It will print only log messages matching REGEXP.  If -f LOGFILE is
# passed, then instead of running "svn log", the log data will be
# read from LOGFILE (which should be in the same format as the
# output of "svn log").
#
# Note:
# In the future, this may take pathnames and/or revision numbers as
# arguments.
#
# ====================================================================
# Copyright (c) 2000-2002 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

use strict;
use Getopt::Long;

my $log_file;

GetOptions('file=s' => \$log_file) or
  &usage;

&usage("$0: too few arguments") unless @ARGV;
&usage("$0: too many arguments") if @ARGV > 1;

my $filter = shift;

my $log_cmd = "svn log";

my $log_separator = '-' x 72 . "\n";

my $open_string = defined $log_file ? $log_file : "$log_cmd |";
open(LOGDATA, $open_string) or
  die "$0: cannot open `$open_string' for reading: $!\n";

my $this_entry_accum = "";
my $this_rev = -1;
my $this_lines = 0;
while (<LOGDATA>)
{
  if (/^rev ([0-9]+):  [^\|]+ \| [^\|]+ \| ([0-9]+) (line|lines)$/)
  {
    $this_rev = $1;
    $this_lines = $2 + 1;  # Compensate for blank line preceding body.

    $this_entry_accum .= $_;
  }
  elsif ($this_lines == 0)  # Reached end of msg.  Looking at log separator?
  {
    if (! ($_ eq $log_separator))
    {
      die "$0: wrong number of lines for log message!\n${this_entry_accum}\n";
    }

    if ($this_entry_accum =~ /$filter/og)
    {
      print "${this_entry_accum}${log_separator}";
    }

    # Reset accumulators.
    $this_entry_accum = "";
    $this_rev = -1;
  }
  elsif ($this_lines < 0)
  {
    die "$0: line weirdness parsing log.\n";
  }
  else   # Must be inside a message, continue accumulating.
  {
    $this_entry_accum .= $_;
    $this_lines--;
  }
}

close(LOGDATA) or
  die "$0: closing `$open_string' failed: $!\n";

exit 0;

sub usage {
  warn "@_\n" if @_;
  die "usage: $0: [-f LOGFILE] REGEXP\n",
      "This script will print only log messages matching REGEXP by\n",
      "running `svn log' in the current working directory.  If\n",
      "-f LOGFILE is passed, then instead of running `svn log', the\n",
      "log data will be read from LOGFILE (which should be in the\n",
      "same format as the output of `svn log').\n";
}

#!/usr/bin/perl -w

# ====================================================================
# Show log messages matching a certain pattern.  Usage:
#
#    search-svnlog.pl [-v] [-f LOGFILE] REGEXP
#
# See &usage() for details.
#
# ====================================================================
# Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
my $invert = 0;
my $caseless = 0;

GetOptions('f|file=s' => \$log_file,
           'v|invert' => \$invert,
           'i|caseinsensitive' => \$caseless) or &usage;

&usage("$0: too few arguments") unless @ARGV;
&usage("$0: too many arguments") if @ARGV > 1;

my $filter = shift;
$filter = '(?i)' . $filter if $caseless;

my $log_cmd = "svn log";

my $log_separator = '-' x 72 . "\n";

my $open_string = defined $log_file ? $log_file : "$log_cmd |";
open(LOGDATA, $open_string) or
  die "$0: cannot open `$open_string' for reading: $!\n";

my $this_entry_accum = "";
my $this_rev = -1;
my $this_lines = 0;
my $seen_blank_line;  # A blank line separates headers from body.

while (<LOGDATA>)
{
  if (/^r([0-9]+) \| [^\|]* \| [^\|]* \| ([0-9]+) (line|lines)$/)
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

    if ($this_entry_accum =~ /$filter/og ^ $invert)
    {
      print "${this_entry_accum}${log_separator}";
    }

    # Reset accumulators.
    $seen_blank_line = 0;
    $this_entry_accum = "";
    $this_rev = -1;
  }
  elsif ($this_lines < 0)
  {
    die "$0: line weirdness parsing log.\n";
  }
  else   # Just continue accumulating.
  {
    $this_entry_accum .= $_;

    if ($seen_blank_line)
    {
      $this_lines--;
    }
    elsif (/^$/)
    {
      $seen_blank_line = 1;
      $this_lines--;
    }
  }
}

close(LOGDATA) or
  die "$0: closing `$open_string' failed: $!\n";

exit 0;

sub usage {
  warn "@_\n" if @_;
  die "usage: $0: [-v] [-i] [-f LOGFILE] REGEXP\n",
      "\n",
      "Print only log messages matching REGEXP, either by running 'svn log'\n",
      "in the current working directory, or if '-f LOGFILE' is passed, then\n",
      "read the log data from LOGFILE (which should be in the same format\n",
      "as the output of 'svn log').\n",
      "\n",
      "If '-v' is given, the matching is inverted (like 'grep -v').\n",
      "\n",
      "If '-i' is given, the matching is case-insensitive (like 'grep -i').\n";
}

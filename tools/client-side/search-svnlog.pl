#!/usr/bin/perl -w

### Show log messages matching a certain pattern.  Usage:
###
###    search-svnlog.pl REGEXP
###
### It will print only log messages matching REGEXP.
### 
### Note:
### In the future, this may take pathnames and/or revision numbers as
### arguments.  Then it will need to do real argument parsing, if
### nothing else to separate the REGEXP from the other arguments.
### Personally, I'm not going to bother with that right now; it's
### useful enough as is.

use strict;

my $filter = shift || die ("Usage: $0 REGEXP\n");
my $log_cmd = "svn log";

my $log_separator =
  "------------------------------------------------------------------------\n";

open (LOG_OUT, "$log_cmd |") or die ("Unable to run \"$log_cmd\".\n");

my $this_entry_accum = "";
my $this_rev = -1;
my $this_lines = 0;
while (<LOG_OUT>)
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
      die ("Wrong number of lines for log message!\n${this_entry_accum}\n");
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
    die ("Line weirdness parsing log.\n");
  }
  else   # Must be inside a message, continue accumulating.
  {
    $this_entry_accum .= $_;
    $this_lines--;
  }
}

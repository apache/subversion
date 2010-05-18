#!/usr/bin/perl -w

use strict;

# ====================================================================
# Show the log message and diff for a revision.
#
#    $ showchange.pl REVISION [WC_PATH|URL]


if ((scalar(@ARGV) == 0)
    or ($ARGV[0] eq '-?')
    or ($ARGV[0] eq '-h')
    or ($ARGV[0] eq '--help')) {
    print <<EOF;
Show the log message and diff for a revision.
usage: $0 REVISION [WC_PATH|URL]
EOF
    exit 0;
}

my $revision = shift || die ("Revision argument required.\n");
if ($revision =~ /r([0-9]+)/) {
  $revision = $1;
}

my $url = shift || "";

my $svn = "svn";

my $prev_revision = $revision - 1;

if (not $url) {
  # If no URL was provided, use the repository root from the current
  # directory's working copy.  We want the root, rather than the URL
  # of the current dir, because when someone's asking for a change
  # by name (that is, by revision number), they generally don't want
  # to have to cd to a particular working copy directory to get it.
  my @info_lines = `${svn} info`;
  foreach my $info_line (@info_lines) {
    if ($info_line =~ s/^Repository Root: (.*)$/$1/e) {
      $url = $info_line;
    }
  }
}

system ("${svn} log -v --incremental -r${revision} $url");
system ("${svn} diff -r${prev_revision}:${revision} $url");

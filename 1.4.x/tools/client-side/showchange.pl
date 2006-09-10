#!/usr/bin/perl -w

use strict;

# ====================================================================
# Show the log message and diff for a revision.
#
#    $ showchange.pl REVISION [PATH|URL]


if ((scalar(@ARGV) == 0)
    or ($ARGV[0] eq '-?')
    or ($ARGV[0] eq '-h')
    or ($ARGV[0] eq '--help')) {
    print <<EOF;
Show the log message and diff for a revision.
usage: $0 REVISION [PATH|URL]
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

system ("${svn} log -v --incremental -r${revision} $url");
system ("${svn} diff -r${prev_revision}:${revision} $url");

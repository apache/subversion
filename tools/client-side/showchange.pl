#!/usr/bin/perl -w

use strict;

# ====================================================================
# Show the log message and diff for a revision.
#
#    $ showchange.pl REVISION [PATH|URL]


my $revision = shift || die ("Revision argument required.\n");

my $url = shift || "";

my $svn = "svn";

my $prev_revision = $revision - 1;

system ("${svn} log --incremental -r${prev_revision}:${revision} $url");
system ("${svn} diff -r${prev_revision}:${revision} $url");

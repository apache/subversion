#!/usr/bin/perl -w
#
# svn-graph.pl - produce a GraphViz .dot graph for the branch history
#                of a node
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
#
# TODO:
#   - pay attention to when branches are deleted
#   - be a bit more careful about following the branch history,
#     it's currently a bit of a quick hack
#   - take some command line parameters (url, start & end revs, 
#     node we're tracking, etc)
#   - calculate the repository root at runtime so the user can pass
#     the node of interest as a single URL
#   - produce the graphical output ourselves (SVG?) instead
#     of using .dot?
#
# NOTES:
#   Everything goes to stdout
#   Algorithm is a bit stuffed.  We're not really reconstructing
#     the DAG here, it just looks like it.  We're close, but I'll
#     have to think harder.
#

use strict;

require SVN::Core;
require SVN::Ra;

# CONFIGURE ME:  The URL of the Subversion repository we wish to graph.
# See TODO.
my $REPOS_URL = 'http://svn.collab.net/repos/svn';

# Point at the root of a repository so we get can look at
# every revision.  
my $ra = SVN::Ra->new($REPOS_URL);

# We're going to look at all revisions
my $youngest = $ra->get_latest_revnum();
my $startrev = 0;

# This is the node we're interested in
my $startpath = "/trunk";

# This array holds all the copies we've followed so far
# TODO: this will have to change if we pay attention to deletes
my @frompaths = ($startpath);

# This holds all the nodes we've visited so that we can ensure
# everything is joined later on
my %copypaths = ();

# This function is a callback which is called for every revision
# as we traverse
sub show_log {
  my $changed_paths = shift;
  my $revision = shift;
  my $author = shift;
  my $date = shift;
  my $message = shift;
  my $pool = shift;

  # Uncomment this if you want some kind of progress indicator
  # print STDERR "$revision ";

  # For each path changed in this revision:
  #   If it's copied from somewhere else
  #   and that somewhere else is a path we're
  #   tracing (exists in @frompaths),
  #     Print out a new node linking
  #     the somewhere else (copyfrom:copyrev)
  #     to this changed path
  #     Also, make note of the two nodes we touched (in %copypaths)
  #     so we can later ensure that they're all linked

  foreach my $key (keys %$changed_paths) {
    my $copyfrom = $$changed_paths{$key}->copyfrom_path;
    my $copyrev = $$changed_paths{$key}->copyfrom_rev;
    if (defined($copyfrom)) {
      if (grep(/^$copyfrom$/, @frompaths)) {
        print "\t\"$copyfrom:$copyrev\" -> \"$key:$revision\";\n";
        push(@frompaths, $key);
        $copypaths{"$copyfrom:$copyrev"} = 1;
        $copypaths{"$key:$revision"} = 1;
      }
    }
  }
}

# Actually do some work
print "digraph branches {\n";

# And we can do it all with just one call to SVN :)
$ra->get_log(['/'], $startrev, $youngest, 1, 0, \&show_log); 

my %paths = ();

# For all nodes that we found that match our
# original starting node, indicate that it should
# be coloured (this makes it easier to see in a big graph)
# Also, store all the revisions we touched a particular
# path for in %paths

foreach my $frompath (keys %copypaths) {
  $frompath =~ m/(.*?):([0-9]+)$/;
  my $p = $1;
  if ($p eq $startpath) {
    print "\t\"$frompath\" [style=\"filled\",fillcolor=\"#cccccc\",color=\"red\"];\n";
  }
  my $r = $2;
  ${$paths{$p}}{$r} = 1;
}

# Now, go through all of the revisions of each path,
# sort them (because revisions are ordered in time),
# and make sure they're linked.
# This gives us a set of links along a path
# TODO: this is broken for situations where path
#   is deleted and then recreated (this loop doesn't notice
#   and joins things that shouldn't be)

foreach my $path (keys %paths) {
  my @values = sort {$a <=> $b} keys %{$paths{$path}};
  for (my $i=0; $i < $#values; $i++) {
    print "\t\"$path:".$values[$i]."\" -> \"$path:".$values[$i+1]."\";\n";
  }
}

print "}\n";

# Uncomment if printing progress indicators
# print STDERR "\n";

#!/usr/bin/perl -w
#
# svn-graph.pl - produce a GraphViz .dot graph for the branch history
#                of a node
#
# WARNING:  Right now, this will produce a very large graph for
#           repositories with lots of revisions that participate
#           in the history of the paths you're graphing.  See the TODO
#           below for a description of what remains to be done to
#           filter out some of the noise.
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
#   - take some command line parameters (url, start & end revs, 
#     node we're tracking, etc)
#   - calculate the repository root at runtime so the user can pass
#     the node of interest as a single URL
#   - produce the graphical output ourselves (SVG?) instead
#     of using .dot?
#   - trim out changes along a codeline that aren't a source of copies
#     to massively reduce the size of the graph.  Right now, every
#     change to a path makes a new node (i.e. there are 10000 nodes
#     for trunk for the Subversion repository).
#

use strict;

require SVN::Core;
require SVN::Ra;

# CONFIGURE ME:  The URL of the Subversion repository we wish to graph.
# See TODO.
my $REPOS_URL = 'file:///some/repository';
#my $REPOS_URL = 'http://svn.collab.net/repos/svn';

# Point at the root of a repository so we get can look at
# every revision.  
my $ra = SVN::Ra->new($REPOS_URL);

# We're going to look at all revisions
my $youngest = $ra->get_latest_revnum();
my $startrev = 1;

# This is the node we're interested in
my $startpath = "/trunk";

# The "interesting" nodes are potential sources for copies.  This list
#   grows as we move through time.
# The "tracking" nodes are the most recent revisions of paths we're
#   following as we move through time.  If we hit a delete of a path
#   we remove it from the tracking array (i.e. we're no longer interested
#   in it).
my %interesting = ();
my %tracking = ("$startpath", $startrev);

# This function is a callback which is called for every revision
# as we traverse
sub process_revision {
  my $changed_paths = shift;
  my $revision = shift || "";
  my $author = shift || "";
  my $date = shift || "";
  my $message = shift || "";
  my $pool = shift;

  print STDERR "$revision ";

  #  print "Revision: $revision\n";
  #  print "Author: $author\n";
  #  print "Date: $date\n";
  #  print "Changes:\n";
  foreach my $path (keys %$changed_paths) {
    my $copyfrom_path = $$changed_paths{$path}->copyfrom_path;
    my $copyfrom_rev = $$changed_paths{$path}->copyfrom_rev;
    my $action = $$changed_paths{$path}->action;

    # See if we're deleting one of our tracking nodes
    if ($action eq "D" and exists($tracking{$path})) 
    {
      print "\"$path:$tracking{$path}\" ";
      print "[label=\"$path:$tracking{$path}\\nDeleted in r$revision\"]\n";
      delete($tracking{$path});
      next;
    }

    # If this is a copy, work out if it was from somewhere interesting
    if (defined($copyfrom_path) && 
        exists($interesting{$copyfrom_path.":".$copyfrom_rev})) 
    {
      $interesting{$path.":".$revision} = 1;
      $tracking{$path} = $revision;
      print "\t\"$copyfrom_path:$copyfrom_rev\" -> ";
      print " \"$path:$revision\" [label=\"copy\",weight=1,color=red];\n";
    }

    # For each change, we'll move up the path, updating any parents
    # that we're tracking (i.e. a change to /trunk/asdf/foo updates
    # /trunk).  We mark that parent as interesting (a potential source
    # for copies), draw a link, and update it's tracking revision.
    while ($path =~ m:/:) {
      if (exists($tracking{$path}) && $tracking{$path} != $revision) {
        print "\t\"$path:$tracking{$path}\" -> \"$path:$revision\" ";
        print "[label=\"change\",weight=10];\n";
        $interesting{$path.":".$revision} = 1;
        $tracking{$path} = $revision;
      }
      $path =~ s:/[^/]+$::;
    }
  }

}

# And we can do it all with just one call to SVN :)
print "digraph tree {\n";

$ra->get_log(['/'], $startrev, $youngest, 1, 0, \&process_revision); 

print "}\n";

#!/usr/bin/perl
###############################################################################
# Tweak Subversion log messages
# -----------------------------
# 
# It sure would be nice to be able to change the log messages on
# committed revisions of the Subversion repository via the web.  This
# is a quick attempt at making that happen.
#
# The idea here is that you visit this script at the web page.  With
# no action supplied, it will present a form asking for the revision
# of the log you wish to change.
#
# Upon submitting the form, it will come back with yet another form,
# which will:
#
#   - Display the current log message as static text.  
#   - Present a textarea for editing, initialized with the current
#     log message.
#
# The user can edit the message in the textarea, then submit that form,
# which will return a confirmation and show the new log message.
#
# ====================================================================
# Copyright (c) 2001-2003 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

###############################################################################

use strict;
use CGI qw(:standard);

###############################################################################
# Configuration Section

my $gSvnlookCmd = '/usr/local/bin/svnlook';
my $gSvnadminCmd = '/usr/local/bin/svnadmin';
my $gReposPath = '/usr/www/repositories/svn';
my $gActionURL = './tweak-log.cgi';
my $gTempfilePrefix = '/tmp/tweak-cgi';
my $gHistoryFile = './TWEAKLOG';
my $gBypassRevpropHooks = 0; # set to 1 to bypass the repository hook system
my $gNumRecentCommits = 20;  # number of recent commits to show on init form
###############################################################################

my %gCGIValues = &doCGI( );
&main( );


#-----------------------------------------------------------------------------#
sub html_escape
# (log)
#-----------------------------------------------------------------------------#
{
  my $str = shift;
  $str =~ s/&/&amp;/g;
  $str =~ s/>/&gt;/g;
  $str =~ s/</&lt;/g;
  return $str;
}


#-----------------------------------------------------------------------------#
sub doCGI
# (void)
#-----------------------------------------------------------------------------#
{
    my $lCGI = new CGI;
    my @lFields = $lCGI->param;
    my $lField;
    my %lCGIData = ();

    foreach $lField ( @lFields )
    {
        $lCGIData{ uc $lField } = $lCGI->param( $lField );
    }
    return( %lCGIData );
}


#-----------------------------------------------------------------------------#
sub doError
# (error)
#-----------------------------------------------------------------------------#
{
    my $error = shift @_;

    print "<html><head><title>Tweak Log - Error</title></head>\n";
    print "<body><h1>ERROR</h1>\n<p>$error</p></body></html>\n";
    return;
}


#-----------------------------------------------------------------------------#
sub main
# (void)
#-----------------------------------------------------------------------------#
{
    # Print out HTTP headers.
    print "Content-type: text/html; charset=UTF-8\n\n";

    # Figure out what action to take.
    if( $gCGIValues{'ACTION'} =~ /fetch/i )
    {
	&doFetchLog();
    }
    elsif( $gCGIValues{'ACTION'} =~ /commit/i )
    {
	&doCommitLog();
    }
    else
    {
	&doInitialForm();
    }
    return;
}


#-----------------------------------------------------------------------------#
sub doInitialForm
# (void)
#-----------------------------------------------------------------------------#
{
    my $youngest = `$gSvnlookCmd youngest $gReposPath`;
    my $rev;
    my $oldest;
    
    print "<html>\n<head>\n<title>Tweak Log</title>\n</head>\n";
    print "<body>\n<form action=\"$gActionURL\" method=\"post\">\n";
    print "<a name=\"__top__\"></a>\n";
    print "<p>\n";
    print "Boy, I sure would like to modify that log message for \n";
    print "revision <input type=\"text\" name=\"rev\" value\"\">\n";
    print "<input type=\"submit\" name=\"action\" value=\"Fetch Log\">\n";
    print "</p></form>\n";
    print "<p>\n";
    print "For convenience, here are the most recent $gNumRecentCommits\n";
    print "commits (click the revision number to edit that revision's log):\n";
    print "</p>\n";
    chomp $youngest;
    $oldest = $youngest - $gNumRecentCommits + 1;
    $oldest = 1 if( $oldest < 1 );
    $rev = $youngest;
    while( $rev >= $oldest )
    {
        my @infolines = `$gSvnlookCmd info $gReposPath -r $rev`;
        my $author = shift @infolines;
        my $date = shift @infolines;
        my $log_size = shift @infolines;

        print "<hr />\n";
        print "<a href=\"$gActionURL?action=Fetch+Log&rev=$rev\">Revision $rev</a>:<br />\n";
        print "<i>Author: $author</i><br />\n";
        print "<i>Date: $date</i><br />\n";
        print "<i>Log: </i><br /><pre>\n";
        map {
            $_ = &html_escape ($_);
        } @infolines;
	print @infolines;
	print "</pre><br />\n";
	print "<a href=\"#__top__\">(back to top)</a>\n";
        $rev--;
    }
    print "</body></html>\n";
    return;
}


#-----------------------------------------------------------------------------#
sub isValidRev
# (rev)
#-----------------------------------------------------------------------------#
{
    my $youngest = `$gSvnlookCmd youngest $gReposPath`;
    my $rev = shift @_;

    if(not (( $youngest =~ /^\d+$/) and
	    ( $youngest > 0 )))
    {
	&doError( "Unable to determine youngest revision" );
	return 0;
    }
    if(not (( $rev =~ /^\d+$/) and
	    ( $rev <= $youngest )))
    {
	&doError( "'$rev' is not a valid revision number" );
	return 0;
    }
    return 1;
}


#-----------------------------------------------------------------------------#
sub doFetchLog
# (void)
#-----------------------------------------------------------------------------#
{
    my $rev = $gCGIValues{'REV'};
    my $log;
    my $escaped_log;   ## HTML-escaped version of $log

    # Make sure we've requested a valid revision.
    if( not &isValidRev( $rev ))
    {
	return;
    }
    
    # Fetch the log for that revision.
    $log = `$gSvnlookCmd log $gReposPath -r $rev`;

    $escaped_log = &html_escape ($log);

    # Display the form for editing the revision
    print "<html>\n<head>\n<title>Tweak Log - Log Edit</title>\n</head>\n";
    print "<body>\n";
    print "<h1>Editing Log Message for Revision $rev</h1>\n";
    print "<h2>Current log message:</h2>\n";
    print "<blockquote><hr /><pre>$escaped_log</pre><hr /></blockquote>\n";
    print "<p><font color=\"red\">\n";
    print "<i>Every change made is logged in <tt>${gHistoryFile}</tt>.\n";
    print "If you make a bogus\n";
    print "change, you can still recover the old message from there.</i>\n";
    print "</font></p>\n";
    print "<form action=\"$gActionURL\" method=\"post\">\n";
    print "<h2>New log message:</h2>\n";
    print "<blockquote>\n";
    print "<textarea cols=\"80\" rows=\"25\" wrap=\"off\" name=\"log\">\n";
    print $escaped_log;
    print "</textarea><br />\n";
    print "<input type=\"hidden\" name=\"rev\" value=\"$rev\">\n";
    print "<input type=\"submit\" name=\"action\" value=\"Commit Changes\">\n";
    print "</blockquote>\n";
    print "</form></body></html>\n";
    return;
}


#-----------------------------------------------------------------------------#
sub doCommitLog
# (void)
#-----------------------------------------------------------------------------#
{
    my $rev = $gCGIValues{'REV'};
    my $log = $gCGIValues{'LOG'};
    my $orig_log;
    my $tempfile = "$gTempfilePrefix.$$";

    # Make sure we are about to change a valid revision.
    if (not &isValidRev( $rev ))
    {
	return;
    }

    # Get the original log from the repository.
    $orig_log = `$gSvnlookCmd log $gReposPath -r $rev`;

    # If nothing was changed, go complain to the user (shame on him for
    # wasting our time like that!)
    if ($log eq $orig_log)
    {
	&doError ("Log message doesn't appear to have been edited.");
	return;
    }
    
    # Open a tempfile
    if (not (open( LOGFILE, "> $tempfile")))
    {
	&doError ("Unable to open temporary file.");
	return;
    }

    # Dump the new log into the tempfile (and close it)
    print LOGFILE $log;
    close LOGFILE;

    # Tell our history file what we're about to do.
    if ($gHistoryFile)
    {
        if (not (open (HISTORY, ">> $gHistoryFile")))
        {
            &doError ("Unable to open history file.");
            return;
        }
        print HISTORY "====================================================\n";
        print HISTORY "REVISION $rev WAS:\n";
        print HISTORY "----------------------------------------------------\n";
        print HISTORY $orig_log;
        print HISTORY "\n";
    }

    # Now, make the mods
    if ($gBypassRevpropHooks)
    {
        `$gSvnadminCmd setlog $gReposPath -r$rev $tempfile --bypass-hooks`;
    }
    else
    {
        `$gSvnadminCmd setlog $gReposPath -r$rev $tempfile`;
    }
    
    # ...and remove the tempfile.  It is, after all, temporary.
    unlink $tempfile;

    # Now, tell the history file what we did.
    if ($gHistoryFile)
    {
        print HISTORY "----------------------------------------------------\n";
        print HISTORY "REVISION $rev IS:\n";
        print HISTORY "----------------------------------------------------\n";
        print HISTORY $log;
        print HISTORY "\n";
        close HISTORY;
    }

    # Now, re-read that logfile
    $log = `$gSvnlookCmd log $gReposPath -r $rev`;
    $log = &html_escape ($log);

    print "<html>\n<head>\n<title>Tweak Log - Log Changed</title>\n</head>\n";
    print "<body>\n";
    print "<h1>Success!</h1>\n";
    print "<h2>New Log Message for Revision $rev</h2>\n";
    print "<blockquote><hr /><pre>$log</pre><hr /></blockquote>\n";
    print "</body></html>\n";
    return;
}

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
###############################################################################

use strict;
use CGI qw(:standard);

###############################################################################
# Configuration Section

my $gSvnlookCmd = '/usr/local/bin/svnlook';
my $gSvnadminCmd = '/usr/local/bin/svnadmin';
my $gReposPath = '/home/cmpilato/tests/repos';
my $gActionURL = './tweak-log.cgi';
my $gTempfilePrefix = '/tmp/tweak-cgi';
###############################################################################

my %gCGIValues = &doCGI( );
&main( );

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
    print "Content-type: text/html\n\n";

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
    print "<html>\n<head>\n<title>Tweak Log</title>\n</head>\n";
    print "<body>\n<form action=\"$gActionURL\" method=\"post\">\n";
    print "<p>\n";
    print "Boy, I sure would like to modify that log message for \n";
    print "revision <input type=\"text\" name=\"rev\" value\"\">\n";
    print "<input type=\"submit\" name=\"action\" value=\"Fetch Log\">\n";
    print "</p></form></body></html>\n";
    return;
}


#-----------------------------------------------------------------------------#
sub isValidRev
# (rev)
#-----------------------------------------------------------------------------#
{
    my $youngest = `$gSvnadminCmd youngest $gReposPath`;
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

    # Make sure we've requested a valid revision.
    if( not &isValidRev( $rev ))
    {
	return;
    }
    
    # Fetch the log for that revision.
    $log = `$gSvnlookCmd $gReposPath rev $rev log`;

    # Display the form for editing the revision
    print "<html>\n<head>\n<title>Tweak Log - Log Edit</title>\n</head>\n";
    print "<body>\n";
    print "<h1>Editing Log Message for Revision $rev</h1>\n";
    print "<h2>Current log message:</h2>\n";
    print "<blockquote><hr /><pre>$log</pre><hr /></blockquote>\n";
    print "<form action=\"$gActionURL\" method=\"post\">\n";
    print "<h2>New log message:</h2>\n";
    print "<blockquote>\n";
    print "<textarea cols=\"80\" rows=\"25\" wrap=\"off\" name=\"log\">\n";
    print $log;
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
    $orig_log = `$gSvnlookCmd $gReposPath rev $rev log`;

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

    # Now, make the mods
    `$gSvnadminCmd setlog $gReposPath $rev $tempfile`;

    # Now, re-read that logfile
    $log = `$gSvnlookCmd $gReposPath rev $rev log`;

    print "<html>\n<head>\n<title>Tweak Log - Log Changed</title>\n</head>\n";
    print "<body>\n";
    print "<h1>Success!</h1>\n";
    print "<h2>New Log Message for Revision $rev</h2>\n";
    print "<blockquote><hr /><pre>$log</pre><hr /></blockquote>\n";
    print "</body></html>\n";
    return;
}

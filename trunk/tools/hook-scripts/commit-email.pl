#!/usr/bin/perl -w

# ====================================================================
# commit-email.pl: send a commit email for commit REVISION in
# repository REPOS to some email addresses.
#
# Usage: commit-email.pl REPOS REVISION [OPTIONS] [EMAIL-ADDR ...]
#
# Options:
#    -h hostname       :  Hostname to append to author for 'From:'
#    -l logfile        :  File to while mail contents should be 
#                         appended
#    -s subject_prefix :  Subject line prefix
#    
# ====================================================================
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

######################################################################
#  CONFIGURATION SECTION
######################################################################

# sendmail path
my $sendmail = "/usr/sbin/sendmail";

# svnlook path
my $svnlook = "/usr/local/bin/svnlook";

######################################################################
# Initial setup/command-line handling

# get the REPOS from the arguments
my $repos = shift @ARGV;

# get the REVISION from the arguments
my $rev = shift @ARGV;

# initialize the EMAIL_ADDRS to the remaining arguments
my @email_addrs = @ARGV;

# now, we see if there are any options included in the argument list
my $logfile = '';
my $hostname = '';
my $subject_prefix = '';
while (scalar @email_addrs)
{
    my $option = shift @email_addrs;
    if ($option eq '-h')
    {
        # found a hostname option
        $hostname = shift @email_addrs;
    }
    elsif ($option eq '-l')
    {
        # found a logfile option
        $logfile = shift @email_addrs;
    }
    elsif ($option eq '-s')
    {
        # found a subject prefix option
        $subject_prefix = shift @email_addrs;
    }
    else
    {
        # not an option, put it back!
        unshift @email_addrs, ($option);
        last;
    }
}

######################################################################
# Harvest data using svnlook

my @svnlooklines = ();
my @output = ();
my $line;

# get the auther, date, and log from svnlook
open (INPUT, "$svnlook $repos rev $rev info |") 
    or die ("Error running svnlook (info)");
@svnlooklines = <INPUT>;
close (INPUT);
my $author = shift @svnlooklines;
my $date = shift @svnlooklines;
shift @svnlooklines;
my @log = @svnlooklines;
chomp $author;
chomp $date;

# figure out what directories have changed (using svnlook)
open (INPUT, "$svnlook $repos rev $rev dirs-changed |")
    or die ("Error running svnlook (changed)");
my @dirschanged = <INPUT>;
my $rootchanged = 0;
close (INPUT);
chomp @dirschanged;
grep 
{
    # lose the trailing slash if one exists (except in the case of '/')
    $rootchanged = 1 if ($_ eq '/');
    $_ =~ s/(.+)[\/\\]$/$1/;
} 
@dirschanged; 

# figure out what's changed (using svnlook)
open (INPUT, "$svnlook $repos rev $rev changed |") 
    or die ("Error running svnlook (changed)");
@svnlooklines = <INPUT>;
close (INPUT);

# parse the changed nodes
my @adds = ();
my @dels = ();
my @mods = ();
foreach $line (@svnlooklines)
{
    my ($code, $path) = split ('   ', $line);

    if ($code eq 'A') {
        push (@adds, "   $path");
    }
    elsif ($code eq 'D') {
        push (@dels, "   $path");
    }
    else {
        push (@mods, "   $path");
    }
}

# get the diff from svnlook
open (INPUT, "$svnlook $repos rev $rev diff |") 
    or die ("Error running svnlook (diff)");
my @difflines = <INPUT>;
close (INPUT);

######################################################################
# Mail headers

# collapse the list of changed directories
my @commonpieces = ();
my $commondir = '';
if (($rootchanged == 0) and (scalar @commonpieces > 1))
{
    my $firstline = shift (@dirschanged);
    push (@commonpieces, split ('/', $firstline));
    foreach $line (@dirschanged)
    {
        my @pieces = ();
        my $i = 0;
        push (@pieces, split ('/', $line));
        while (($i < scalar @pieces) and ($i < scalar @commonpieces))
        {
            if ($pieces[$i] ne $commonpieces[$i])
            {
                splice (@commonpieces, $i, (scalar @commonpieces - $i));
                last;
            }
            $i++;
        }
    }
    unshift (@dirschanged, $firstline);
    if (scalar @commonpieces)
    {
        $commondir = join ('/', @commonpieces);
        grep
        {
            s/^$commondir\/(.*)/$1/eg;
        }
        @dirschanged;
    }
}
my $dirlist = join (' ', @dirschanged);


my $userlist = join (' ', @email_addrs); 
my $subject;
if ($commondir ne '')
{
    $subject = "rev $rev - in $commondir: $dirlist";
}
else
{
    $subject = "rev $rev - $dirlist";
}
if ($subject_prefix =~ /\w/)
{
    $subject = "$subject_prefix $subject";
}
my $mail_from = $author;
if ($hostname =~ /\w/)
{
    $mail_from = "$mail_from\@$hostname";
}
push (@output, ("To: $userlist\n"));
push (@output, ("From: $mail_from\n"));
push (@output, ("Subject: $subject\n"));
push (@output, ("Reply-to: dev\@subversion.tigris.org\n"));
push (@output, ("\n"));

# mail body
push (@output, ("Author: $author\n"));
push (@output, ("Date: $date\n"));
push (@output, ("New Revision: $rev\n"));
push (@output, ("\n"));
if (scalar @adds)
{
    @adds = sort @adds;
    push (@output, ("Added:\n"));
    push (@output, (@adds));
}
if (scalar @dels)
{
    @dels = sort @dels;
    push (@output, ("Removed:\n"));
    push (@output, (@dels));
}
if (scalar @mods)
{
    @mods = sort @mods;
    push (@output, ("Modified:\n"));
    push (@output, (@mods));
}
push (@output, ("Log:\n"));
push (@output, (@log));
push (@output, ("\n"));
push (@output, (@difflines));


# dump output to logfile (if its name is not empty)
if ($logfile =~ /\w/)
{
    open (LOGFILE, ">> $logfile") 
        or die ("Error opening '$logfile' for append");
    print LOGFILE @output;
    close LOGFILE;
}

# open a pipe to 'sendmail'
if (($sendmail =~ /\w/) and ($userlist =~ /\w/))
{
    open (SENDMAIL, "| $sendmail $userlist") 
        or die ("Error opening a pipe to sendmail");
    print SENDMAIL @output;
    close SENDMAIL;
}

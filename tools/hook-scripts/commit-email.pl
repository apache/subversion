#!/usr/bin/perl -w

### commit-email.pl: send a commit email for commit NEW-REVISION in
### REPOSITORY to some email addresses.  Usage:
###
###    commit-email.pl REPOSITORY NEW-REVISION [EMAIL-ADDR ...]
###

use strict;

my $repos = shift @ARGV;
my $rev = shift @ARGV;
my @users = @ARGV;
my @svnlooklines = ();

# open a pipe to 'mail'
my $userlist = join (' ', @users); 
open (MAILER, "| mail -s 'Commit' $userlist") 
    or die ("Error opening a pipe to your stupid mailer");

# get the auther, date, and log from svnlook
open (INPUT, "svnlook $repos rev $rev info |") 
    or die ("Error running svnlook (info)");
@svnlooklines = <INPUT>;
close (INPUT);
my $author = shift @svnlooklines;
my $date = shift @svnlooklines;
shift @svnlooklines;
my @log = @svnlooklines;
chomp $author;
chomp $date;

# figure out what's changed (using svnlook)
open (INPUT, "svnlook $repos rev $rev changed |") 
    or die ("Error running svnlook (changed)");
@svnlooklines = <INPUT>;
close (INPUT);

# parse the changed nodes
my @adds = ();
my @dels = ();
my @mods = ();
foreach my $line (@svnlooklines)
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
open (INPUT, "svnlook $repos rev $rev diff |") 
    or die ("Error running svnlook (diff)");
my @difflines = <INPUT>;
close (INPUT);

print MAILER "Author: $author\nDate: $date\n\n";
if (scalar @adds)
{
    @adds = sort @adds;
    print MAILER "Added:\n";
    print MAILER @adds;
}
if (scalar @dels)
{
    @dels = sort @dels;
    print MAILER "Removed:\n";
    print MAILER @dels;
}
if (scalar @mods)
{
    @mods = sort @mods;
    print MAILER "Modified:\n";
    print MAILER @mods;
}
print MAILER "Log:\n", @log, "\n";
print MAILER @difflines;

close (MAILER);

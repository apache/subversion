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

# open a pipe to 'mail'
my $userlist = join (' ', @users); 
open (MAILER, "| mail -s 'Commit' $userlist") 
    or die ("Error opening a pipe to your stupid mailer");

# open a pipe from svnlook
open (INPUT, "svnlook $repos rev $rev |") 
    or die ("Error running svnlook");
my @svnlooklines = <INPUT>;
close (INPUT);

# parse the author, date, and log message
chomp @svnlooklines;
my $author = shift @svnlooklines;
my $date = shift @svnlooklines;

# open a pipe from svnlook
open (INPUT, "svnlook $repos rev $rev log |") 
    or die ("Error running svnlook");
@svnlooklines = <INPUT>;
close (INPUT);

my @log = @svnlooklines; # something else, obviously.

# open a pipe from svnlook
open (INPUT, "svnlook $repos rev $rev changed |") 
    or die ("Error running svnlook");
@svnlooklines = <INPUT>;
close (INPUT);

# parse the changed nodes
my %path_mods = ();
foreach my $line (@svnlooklines)
{
  chomp $line;

  my ($code, $path) = split ('   ', $line);

  if (defined ($path_mods{$code}) && ($path_mods{$code} ne '')) {
    $path_mods{$code} = $path_mods{$code} . ' ' . $path;
  }
  else {
    $path_mods{$code} = $path;
  }
}

# open a pipe from svnlook
open (INPUT, "svnlook $repos rev $rev diff |") 
    or die ("Error running svnlook");
@svnlooklines = <INPUT>;
close (INPUT);

print MAILER "Author: $author\nDate: $date\n\n";
if (defined ($path_mods{'A'}) && ($path_mods{'A'} ne '')) {
  print MAILER "Added: ", $path_mods{ 'A' }, "\n" ;
}
if (defined ($path_mods{'D'}) && ($path_mods{'D'} ne '')) {
  print MAILER "Removed: ", $path_mods{ 'D' }, "\n";
}
if (defined ($path_mods{'U'}) && ($path_mods{'U'} ne '')) {
  print MAILER "Modified: ", $path_mods{ 'U' }, "\n" ;
}
print MAILER "Log:\n", @log, "\n";
print MAILER @svnlooklines;

close (MAILER);

#!/usr/bin/perl -w

# a script to munge the output of 'svn log' into something approaching the 
# style of a GNU ChangeLog.
#
# to use this, just fill in the 'hackers' hash with the usernames and 
# name/emails of the people who work on your project, go to the top level 
# of your working copy, and run:
#
# $ svn log | /path/to/gnuify-changelog.pl > ChangeLog

%hackers = ( "jimb"        => 'Jim Blandy <jimb@redhat.com>',
             "sussman"     => 'Ben Collins-Sussman <sussman@collab.net>',
             "kfogel"      => 'Karl Fogel <kfogel@collab.net>',
             "gstein"      => 'Greg Stein <gstein@lyra.org>',
             "brane"       => 'Branko Cibej <brane@xbc.nu>',
             "joe"         => 'Joe Orton <joe@light.plus.com>',
             "ghudson"     => 'Greg Hudson <ghudson@mit.edu>',
             "lefty"       => 'Lee P. W. Burgess <lefty@red-bean.com>',
             "fitz"        => 'Brian Fitzpatrick <fitz@red-bean.com>',
             "mab"         => 'Matthew Braithwaite <matt@braithwaite.net>',
             "daniel"      => 'Daniel Stenberg <daniel@haxx.se>',
             "mmurphy"     => 'Mark Murphy <mmurphy@collab.net>',
             "cmpilato"    => 'C. Michael Pilato <cmpilato@collab.net>',
             "kevin"       => 'Kevin Pilch-Bisson <kevin@pilch-bisson.net>',
             "philip"      => 'Philip Martin <philip@codematters.co.uk>',
             "jerenkrantz" => 'Justin Erenkrantz <jerenkrantz@apache.org>',
             "rooneg"      => 'Garrett Rooney <rooneg@electricjellyfish.net>',
             "bcollins"    => 'Ben Collins <bcollins@debian.org>',
             "blair"       => 'Blair Zajac <blair@orcaware.com>',
             "striker"     => 'Sander Striker <striker@apache.org>',
             "XelaRellum"  => 'Alexander Mueller <alex@littleblue.de>',
             "yoshiki"     => 'Yoshiki Hayashi <yoshiki@xemacs.org>',
             "david"       => 'David Summers <david@summersoft.fay.ar.us>',
             "rassilon"    => 'Bill Tutt <rassilon@lyra.org>',
             "kbohling"    => 'Kirby C. Bohling <kbohling@birddog.com>', );

$parse_next_line = 0;

while (<>) {
  # axe windows style line endings, since we should try to be consistent, and 
  # the repos has both styles in it's log entries.
  $_ =~ s/\r\n$/\n/;

  if (/^-+$/) {
    # we're at the start of a log entry, so we need to parse the next line
    $parse_next_line = 1;
  } elsif ($parse_next_line) {
    # transform from svn style to GNU style
    $parse_next_line = 0;

    @parts = split (/ /, $_);

    print "$parts[5] $hackers{$parts[3]}\n";
  } else {
    print "\t$_";
  }
}

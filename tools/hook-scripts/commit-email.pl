#!/usr/bin/perl -w

# ====================================================================
# commit-email.pl: send a commit email for commit REVISION in
# repository REPOS to some email addresses.
#
# For usage, see the usage subroutine or run the script with no
# command line arguments.
#    
# ====================================================================
# Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
use Carp;
use Storable qw(dclone);

######################################################################
#  CONFIGURATION SECTION
######################################################################

# sendmail path
my $sendmail = "/usr/sbin/sendmail";

# svnlook path
my $svnlook = "/usr/local/bin/svnlook";

######################################################################
# Initial setup/command-line handling

# This is the blank information for one project.  Copies of this are
# made as needed for each new project using Storable::dclone.
my $blank_settings = {email_addresses => [],
                      hostname        => '',
                      log_file        => '',
                      match_regex     => '.',
                      reply_to        => '',
                      subject_prefix  => ''};

# Each value in this array holds a hash reference which contains the
# associated email information for one project.  Start with an
# implicit rule that matches all paths.
my @project_settings_list = (dclone($blank_settings));

# Process the command line arguments till there are none left.  The
# first two arguments that are not used by a command line option are
# the repository path and the revision number.
my $repos;
my $rev;

# Use the reference to the first project to populate.
my $current_project = $project_settings_list[0];

while (@ARGV) {
  my $arg = shift @ARGV;
  if (my ($opt) = $arg =~ /^-([hlmrs])/) {
    unless (@ARGV) {
      die "$0: command line option `$arg' is missing a value.\n";
    }
    my $value = shift @ARGV;
    SWITCH: {
      $current_project->{hostname}       = $value, last SWITCH if $opt eq 'h';
      $current_project->{log_file}       = $value, last SWITCH if $opt eq 'l';
      $current_project->{reply_to}       = $value, last SWITCH if $opt eq 'r';
      $current_project->{subject_prefix} = $value, last SWITCH if $opt eq 's';

      # Here handle -match.
      unless ($opt eq 'm') {
        die "$0: internal error: should only handle -m here.\n";
      }
      $current_project = dclone($blank_settings);
      $current_project->{match_regex} = $value;
      push(@project_settings_list, $current_project);
    }
  } elsif ($arg =~ /^-/) {
    die "$0: command line option `$arg' is not recognized.\n";
  } else {
    if (! defined $repos) {
      $repos = $arg;
    } elsif (! defined $rev) {
      $rev = $arg;
    } else {
      push(@{$current_project->{email_addresses}}, $arg);
    }
  }
}

# If the revision number is undefined, then there were not enough
# command line arguments.
&usage("$0: too few arguments") unless defined $rev;

# Check that all of the regular expressions can be compiled and
# compile them.
{
  my $ok = 1;
  for (my $i=0; $i<@project_settings_list; ++$i) {
    my $match_regex = $project_settings_list[$i]->{match_regex};
    my $match_re;
    eval { $match_re = qr/$match_regex/ };
    if ($@) {
      warn "$0: -match regex #$i `$match_regex' does not compile:\n$@\n";
      $ok = 0;
      next;
    }
    $project_settings_list[$i]->{match_re} = $match_re;
  }
  exit 1 unless $ok;
}

######################################################################
# Harvest data using svnlook

# change into /tmp so that svnlook diff can create its .svnlook directory
my $tmp_dir = '/tmp';
chdir($tmp_dir)
    or die "$0: cannot chdir `$tmp_dir': $!\n";

# get the auther, date, and log from svnlook
my @svnlooklines = &read_from_process($svnlook, $repos, 'rev', $rev, 'info');
my $author = shift @svnlooklines;
my $date = shift @svnlooklines;
shift @svnlooklines;
my @log = map { "$_\n" } @svnlooklines;

# figure out what directories have changed (using svnlook)
my @dirschanged = &read_from_process($svnlook, $repos,
                                     'rev', $rev, 'dirs-changed');
my $rootchanged = 0;
grep 
{
    # lose the trailing slash if one exists (except in the case of '/')
    $rootchanged = 1 if ($_ eq '/');
    $_ =~ s/(.+)[\/\\]$/$1/;
} 
@dirschanged; 

# figure out what's changed (using svnlook)
@svnlooklines = &read_from_process($svnlook, $repos, 'rev', $rev, 'changed');

# parse the changed nodes
my @adds = ();
my @dels = ();
my @mods = ();
foreach my $line (@svnlooklines)
{
    my $path = '';
    my $code = '';

    # split the line up into the modification code (ignore propmods) and path
    if ($line =~ /^(.).  (.*)$/)
    {
        $code = $1;
        $path = $2;
    }

    if ($code eq 'A') {
        push (@adds, "   $path\n");
    }
    elsif ($code eq 'D') {
        push (@dels, "   $path\n");
    }
    else {
        push (@mods, "   $path\n");
    }
}

# get the diff from svnlook
my @difflines = &read_from_process($svnlook, $repos, 'rev', $rev, 'diff');

######################################################################
# Mail headers

# collapse the list of changed directories
my @commonpieces = ();
my $commondir = '';
if (($rootchanged == 0) and (scalar @commonpieces > 1))
{
    my $firstline = shift (@dirschanged);
    push (@commonpieces, split ('/', $firstline));
    foreach my $line (@dirschanged)
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

# Put together the body of the log message.
my @body;
push(@body, "Author: $author\n");
push(@body, "Date: $date\n");
push(@body, "New Revision: $rev\n");
push(@body, "\n");
if (scalar @adds) {
  @adds = sort @adds;
  push(@body, "Added:\n");
  push(@body, @adds);
}
if (scalar @dels) {
  @dels = sort @dels;
  push(@body, "Removed:\n");
  push(@body, @dels);
}
if (scalar @mods) {
  @mods = sort @mods;
  push(@body, "Modified:\n");
  push(@body, @mods);
}
push(@body, "Log:\n");
push(@body, @log);
push(@body, "\n");
push(@body, map{ "$_\n" } @difflines);

# Go through each project and see if there are any matches for this
# project.  If so, send the log out.
foreach my $project (@project_settings_list) {
  my $match_re = $project->{match_re};
  my $match    = 0;
  foreach my $path (@dirschanged, @adds, @dels, @mods) {
    if ($path =~ $match_re) {
      $match = 1;
      last;
    }
  }

  next unless $match;

  my @email_addresses = @{$project->{email_addresses}};
  my $userlist        = join(' ', @email_addresses);
  my $hostname        = $project->{hostname};
  my $log_file        = $project->{log_file};
  my $reply_to        = $project->{reply_to};
  my $subject_prefix  = $project->{subject_prefix};
  my $subject;

  if ($commondir ne '') {
    $subject = "rev $rev - in $commondir: $dirlist";
  } else {
    $subject = "rev $rev - $dirlist";
  }
  if ($subject_prefix =~ /\w/) {
    $subject = "$subject_prefix $subject";
  }
  my $mail_from = $author;

  if ($hostname =~ /\w/) {
    $mail_from = "$mail_from\@$hostname";
  }

  my @head;
  push(@head, "To: $userlist\n");
  push(@head, "From: $mail_from\n");
  push(@head, "Subject: $subject\n");
  push(@head, "Reply-to: $reply_to\n") if $reply_to;
  push(@head, "\n");

  if ($sendmail =~ /\w/ and @email_addresses) {
    # Open a pipe to sendmail.
    my $command = "$sendmail $userlist";
    if (open (SENDMAIL, "| $command")) {
      print SENDMAIL @head, @body;
      close SENDMAIL or
        warn "$0: error in closing `$command' for writing: $!\n";
    } else {
      warn "$0: cannot open `| $command' for writing: $!\n";
    }
  }

  # Dump the output to logfile (if its name is not empty).
  if ($log_file =~ /\w/) {
    if (open(LOGFILE, ">> $log_file")) {
      print LOGFILE @head, @body;
      close LOGFILE or
        warn "$0: error in closing `$log_file' for appending: $!\n";
    } else {
        warn "$0: cannot open `$log_file' for appending: $!\n";
    }
  }
}

exit 0;

sub usage {
  warn "@_\n" if @_;
  die "usage: $0 REPOS REVNUM [[-m regex] [options] [email_addr ...]] ...\n",
      "options are\n",
      "  -h hostname        Hostname to append to author for 'From:'\n",
      "  -l logfile         File to which mail contents should be appended\n",
      "  -m regex           Regular expression to match committed path\n",
      "  -r email_address   Set email Reply-To header to this email address\n",
      "  -s subject_prefix  Subject line prefix\n",
      "\n",
      "This script supports a single repository with multiple projects,\n",
      "where each project receives email only for commits that modify that\n",
      "project.  A project is identified by using the -m command line\n",
      "with a regular expression argument.  If a commit has a path that\n",
      "matches the regular expression, then the entire commit matches.\n",
      "Any of the following -h, -l, -r and -s command line options and\n",
      "following email addresses are associated with this project.  The\n",
      "next -m resets the -h, -l, -r and -s command line options and the\n",
      "list of email addresses.\n",
      "\n",
      "To support a single project and the old usage, the script\n",
      "initializes itself with an implicit -m . rule that matches any\n",
      "modifications to the repository.  Therefore, to use the script for\n",
      "a single project repository, just use the other comand line options\n",
      "and list email addresses on the command line as before.  If you do\n",
      "not want a project that matches the entire repository, then use a -m\n",
      "and a regular expression before any other command line options or\n",
      "email addresses.\n";
}

sub safe_read_from_pipe {
  unless (@_) {
    croak "$0: safe_read_from_pipe passed no arguments.\n";
  }
  print "Running @_\n";
  my $pid = open(SAFE_READ, '-|');
  unless (defined $pid) {
    die "$0: cannot fork: $!\n";
  }
  unless ($pid) {
    open(STDERR, ">&STDOUT") or
      die "$0: cannot dup STDOUT: $!\n";
    exec(@_) or
      die "$0: cannot exec `@_': $!\n";
  }
  my @output;
  while (<SAFE_READ>) {
    chomp;
    push(@output, $_);
  }
  close(SAFE_READ);
  my $result = $?;
  my $exit   = $result >> 8;
  my $signal = $result & 127;
  my $cd     = $result & 128 ? "with core dump" : "";
  if ($signal or $cd) {
    warn "$0: pipe from `@_' failed $cd: exit=$exit signal=$signal\n";
  }
  if (wantarray) {
    return ($result, @output);
  } else {
    return $result;
  }
}

sub read_from_process {
  unless (@_) {
    croak "$0: read_from_process passed no arguments.\n";
  }
  my ($status, @output) = &safe_read_from_pipe(@_);
  if ($status) {
    return ("$0: @_ failed with this output:", @output);
  } else {
    return @output;
  }
}

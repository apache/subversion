#!/usr/bin/env perl

# ====================================================================
# check-mime-type.pl: check that every added or property-modified file
# has the svn:mime-type property set and every added or property-modified
# file with a mime-type matching text/* also has svn:eol-style set.
# If any file fails this test the user is sent a verbose error message
# suggesting solutions and the commit is aborted.
#
# Usage: check-mime-type.pl REPOS TXN-NAME
# ====================================================================
# Most of check-mime-type.pl was taken from
# commit-access-control.pl, Revision 9986, 2004-06-14 16:29:22 -0400.
# ====================================================================
# Copyright (c) 2000-2009 CollabNet.  All rights reserved.
# Copyright (c) 2010-2020 Apache Software Foundation (ASF).
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
# ====================================================================

# Turn on warnings the best way depending on the Perl version.
BEGIN {
  if ( $] >= 5.006_000)
    { require warnings; import warnings; }
  else
    { $^W = 1; }
}

use strict;
use Carp;


######################################################################
# Configuration section.

# Toggle: Check files of mime-type text/* for svn:eol-style property.
my $check_text_eol = 1;

# Toggle: Check property-modified files too.
my $check_prop_modified_files = 0;

# Svnlook path.
my $svnlook = "/usr/bin/svnlook";

# Since the path to svnlook depends upon the local installation
# preferences, check that the required program exists to insure that
# the administrator has set up the script properly.
{
  my $ok = 1;
  foreach my $program ($svnlook)
    {
      if (-e $program)
        {
          unless (-x $program)
            {
              warn "$0: required program `$program' is not executable, ",
                   "edit $0.\n";
              $ok = 0;
            }
        }
      else
        {
          warn "$0: required program `$program' does not exist, edit $0.\n";
          $ok = 0;
        }
    }
  exit 1 unless $ok;
}

######################################################################
# Initial setup/command-line handling.

&usage unless @ARGV == 2;

my $repos        = shift;
my $txn          = shift;

unless (-e $repos)
  {
    &usage("$0: repository directory `$repos' does not exist.");
  }
unless (-d $repos)
  {
    &usage("$0: repository directory `$repos' is not a directory.");
  }

# Define two constant subroutines to stand for read-only or read-write
# access to the repository.
sub ACCESS_READ_ONLY  () { 'read-only' }
sub ACCESS_READ_WRITE () { 'read-write' }


######################################################################
# Harvest data using svnlook.

# Change into /tmp so that svnlook diff can create its .svnlook
# directory.
my $tmp_dir = '/tmp';
chdir($tmp_dir)
  or die "$0: cannot chdir `$tmp_dir': $!\n";

# Figure out what files have been added/property-modified using svnlook.
my $regex_files_to_check;
if ($check_prop_modified_files)
  {
    $regex_files_to_check = qr/^(?:A.|.U)  (.*[^\/])$/;
  }
else
  {
    $regex_files_to_check = qr/^A.  (.*[^\/])$/;
  }
my @files_to_check;
foreach my $line (&read_from_process($svnlook, 'changed', $repos, '-t', $txn))
  {
    # Add only files that were added/property-modified to @files_to_check
    if ($line =~ /$regex_files_to_check/)
      {
        push(@files_to_check, $1);
      }
  }

my @errors;
foreach my $path ( @files_to_check )
	{
		my $mime_type;
		my $eol_style;

		# Parse the complete list of property values of the file $path to extract
		# the mime-type and eol-style

		my @output = &read_from_process($svnlook, 'proplist', $repos, '-t',
					$txn, '--verbose', '--', $path);
		my $output_line = 0;

		foreach my $prop (@output)
			{
				if ($prop =~ /^\s*svn:mime-type( : (\S+))?/)
					{
						$mime_type = $2;
						# 1.7.8 (r1416637) onwards changed the format of svnloop proplist --verbose
						# from propname : propvalue format, to values in an indent list on following lines
						if (not $mime_type)
							{
								if ($output_line + 1 >= scalar(@output))
									{
										die "$0: Unexpected EOF reading proplist.\n";
									}
								my $next_line_pval_indented = $output[$output_line + 1];
								if ($next_line_pval_indented =~ /^\s{4}(.*)/)
									{
										$mime_type = $1;
									}
							}
					}
				elsif ($prop =~ /^\s*svn:eol-style( : (\S+))?/)
					{
						$eol_style = $2;
						if (not $eol_style)
							{
								if ($output_line + 1 >= scalar(@output))
									{
										die "$0: Unexpected EOF reading proplist.\n";
									}
								my $next_line_pval_indented = $output[$output_line + 1];
								if ($next_line_pval_indented =~ /^\s{4}(.*)/)
									{
										$eol_style = $1;
									}
							}
					}
				$output_line++;
			}

		# Detect error conditions and add them to @errors
		if (not $mime_type)
			{
				push @errors, "$path : svn:mime-type is not set";
			}
		elsif ($check_text_eol and $mime_type =~ /^text\// and not $eol_style)
			{
				push @errors, "$path : svn:mime-type=$mime_type but svn:eol-style is not set";
			}
	}

# If there are any errors list the problem files and give information
# on how to avoid the problem. Hopefully people will set up auto-props
# and will not see this verbose message more than once.
if (@errors)
  {
    my $addition1 = '';
    my $addition2 = '';
    my $addition3 = '';
    if ($check_prop_modified_files)
      {
        $addition1 = '/property-modified';
      }
    if ($check_text_eol)
      {
        $addition2 = "    In addition text files must have the svn:eol-style property set.\n";
        $addition3 = "    svn propset svn:eol-style native path/of/file\n";
      }
    warn "$0:\n\n",
         join("\n", @errors), "\n\n",
				 <<"EOS";

    Every added$addition1 file must have the svn:mime-type property set.
$addition2
    For binary files try running
    svn propset svn:mime-type application/octet-stream path/of/file

    For text files try
    svn propset svn:mime-type text/plain path/of/file
$addition3
    You may want to consider uncommenting the auto-props section
    in your ~/.subversion/config file. Read the Subversion book
    (http://svnbook.red-bean.com/), Chapter 7, Properties section,
    Automatic Property Setting subsection for more help.
EOS
    exit 1;
  }
else
  {
    exit 0;
  }

sub usage
{
  warn "@_\n" if @_;
  die "usage: $0 REPOS TXN-NAME\n";
}

sub safe_read_from_pipe
{
  unless (@_)
    {
      croak "$0: safe_read_from_pipe passed no arguments.\n";
    }
  print "Running @_\n";
  my $pid = open(SAFE_READ, '-|', @_);
  unless (defined $pid)
    {
      die "$0: cannot fork: $!\n";
    }
  unless ($pid)
    {
      open(STDERR, ">&STDOUT")
        or die "$0: cannot dup STDOUT: $!\n";
      exec(@_)
        or die "$0: cannot exec `@_': $!\n";
    }
  my @output;
  while (<SAFE_READ>)
    {
      chomp;
      push(@output, $_);
    }
  close(SAFE_READ);
  my $result = $?;
  my $exit   = $result >> 8;
  my $signal = $result & 127;
  my $cd     = $result & 128 ? "with core dump" : "";
  if ($signal or $cd)
    {
      warn "$0: pipe from `@_' failed $cd: exit=$exit signal=$signal\n";
    }
  if (wantarray)
    {
      return ($result, @output);
    }
  else
    {
      return $result;
    }
}

sub read_from_process
  {
  unless (@_)
    {
      croak "$0: read_from_process passed no arguments.\n";
    }
  my ($status, @output) = &safe_read_from_pipe(@_);
  if ($status)
    {
      if (@output)
        {
          die "$0: `@_' failed with this output:\n", join("\n", @output), "\n";
        }
      else
        {
          die "$0: `@_' failed with no output.\n";
        }
    }
  else
    {
      return @output;
    }
}

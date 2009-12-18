#!/usr/bin/perl -w

# ====================================================================
#
# svn_update.pl
#
# Put this in your path somewhere and make sure it has exec perms.
# Do your thing w/subversion but when you would use 'svn update'
# call this script instead.
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


# WHY THE NEED FOR THIS SCRIPT?
#
# Currently, the subversion server will attempt to stream all file
# data to the client at once for _each_ merge candidate.  For cases
# that have >1 file and/or the complexity of the merge for any
# given file(s) that would require >n minutes, where n is the
# server's magic timeout (5 min.??), the server will timeout.  This
# leaves the client/user in an unswell state.  See issue #2048 for
# details http://subversion.tigris.org/issues/show_bug.cgi?id=2048.
#
# One solution is to wrap the 'svn update' command in a script that
# will perform the update one file at a time.  The problem with
# this solution is that it defeats the beneficial atomic nature of
# subversion for this type of action.  If commits are still coming
# in to the repository, the value of "HEAD" might change between each
# of these update operations.
#
# Another solution, the one that this script utilizes, passes the
# --diff3-cmd directive to 'svn update' using a command which forces
# a failed contextual merge ('/bin/false', for example).  These faux
# merge failures cause subversion to leave all of the accounting files
# involved in a merge behind and puts them into the 'conflict'
# state.  Since all the data required for all the merges took place
# at that exact moment atomicity is preserved and life is swell.

#######################################################################

# This is required for copy() command.  I believe it's a standard
# module.  If there's a doubt run this from a shell:
#	perl -e 'use File::Copy;'
# If you don't get any complaint from perl you're good.  Otherwise
# comment this line out and change the $backup_mine_files to 0.
use File::Copy;

# This forces backing up of the .mine files for reference even
# after the resolved command.  The backups will be stored as
# <filename.username>
$backup_mine_files=1;

# Choose your favorite graphical diff app.  If it's not here, just add
# the full path to it and the style of the options to the
# %DIFF3CMD_hash below.
$DIFF3CMD="xcleardiff";

# Override the diff3-cmd in the config file here.
# If this is an empty string it'll use the config file's
# setting.
#$d3cmdoverride="";
$d3cmdoverride="/bin/false";

# Add more diff programs here.
# For the internal, discovered, file parameters:
#	+A+ ==> mine
#	+B+ ==> older
#	+C+ ==> latest
#	+D+ ==> Destination (The output of your merged code would go here.
#                            This would, generally, be whatever
#                            $(basename <+A+> .mine) would evaluate to.
# But you can feel free to do something like these:
# 	"+B+ +C+ +A+ -out +D+.bob"
# 	"+B+ +A+ +C+ -out /tmp/bob"
# Just note that the '+' (plus) are to limit the false positives in the
# search and replace sub.
#
# HAVING THE CORRECT PATH, AND ARGS FOR YOUR DIFF PROGRAM, IS CRITICAL!!
%DIFF3CMD_hash=(# Ahh...hallowed be thy name.
                "/opt/atria/bin/xcleardiff" => "+A+ +B+ +C+ -out +D+",
                # This one is slow.
                "/usr/bin/kdiff3"           => "+A+ +B+ +C+ -o +D+",
                # This one's slow and it sucks!(BUGGY)
                "/usr/bin/xxdiff"           => "-M +D+ +A+ +B+ +C+",
                # This one's even worse (no output filename).
                "/usr/bin/meld"             => "+A+ +B+ +C+",
                );

sub exec_cmd
{
  my @args=@_;
  my $CMD=$args[0];
  my @retData;
  my $i=0;

  open (FH,$CMD) || die("Can't '$CMD': $!\n");
  while($_=<FH>)
  {
    chomp($_);
    $retData[$i]=$_;
    $i++;
  }
  close(FH);

  return \@retData;

} # exec_cmd

sub diff_it
{
  my @args=@_;
  my $A=$args[0]; # mine
  my $B=$args[1]; # older
  my $C=$args[2]; # latest
  my $D=$args[3]; # output of merge (Destination)
  my @rdat;

  # What's is the diff of choice?
  if( $CHOSENDIFF eq "" )
  {
    # Glean our choice.
    diff_of_choice();
  }

  # $CHOSENDIFF has data.  We deal with the args.
  ($diffcmd,$diff_format)=(split /:/,$CHOSENDIFF);

  # This works.
  $diff_format=~s/\+A\+/$A/g;
  $diff_format=~s/\+B\+/$B/g;
  $diff_format=~s/\+C\+/$C/g;
  $diff_format=~s/\+D\+/$D/g;

  @rdat=@{exec_cmd("$diffcmd $diff_format 2>/dev/null |")};

  return @rdat;

} #diff_it

sub diff_of_choice
{
  foreach $diff_app (sort(keys(%DIFF3CMD_hash)))
  {
    if( ${diff_app}=~m/${DIFF3CMD}/o )
    {
      $CHOSENDIFF="$diff_app:$DIFF3CMD_hash{$diff_app}";
    }
  }
  if( $CHOSENDIFF eq "" )
  {
    # Big problem.  Some kind of disconnect w/the choice and the hash.
    # Most likely a typo.
    print "It would seem that the '${DIFF3CMD}' was not found in\n";
    print "the hash of diff applications I know about.  Please\n";
    print "investigate and correct.\n";
    exit(1);
  }

} #diff_of_choice

sub svn_update_info
{
  my @data;
  my @file_array;
  my $j;

  # Check to see if the d3cmdoverride is set so
  # we don't fail here if it wasn't.
  if( ${d3cmdoverride} eq "" )
  {
    $d3cmdoverride_final="";
  }
  else
  {
    $d3cmdoverride_final="--diff3-cmd=${d3cmdoverride}";
  }

  @data=@{exec_cmd("svn update ${d3cmdoverride_final} | grep ^C | awk '{print \$2}' |")};
  for($j=0;$j<(scalar(@data));$j++)
  {
    push( @file_array, exec_cmd("svn info $data[$j] |") );
  }

  return @file_array;

} # svn_update_info

sub parse_it
{
  my @file_array=@_;
  my @file;
  my $fname;
  my $fpath_tmp;
  my $fpath;
  my $file_ref;
  my $sbox_repo_base;
  my $sbox_repo_changed;
  my $sbox_repo_latest;
  my @cleanup_array;
  my @commit_array;
  my $rdat;
  my $retline;
  my $i;

  foreach $file_ref (@file_array)
  {
    @file=@{$file_ref};
    for($i=0; $i < (scalar(@file)-1);$i++)
    {
      if( $file[$i]=~m/Name:/o )
      {
        # Key off of "Name:" and then back up one to get "Path:".
        # This way the calculations will be correct when chopping off
        # the name portion on the path bit.
        $fname=(split /Name: /,$file[$i])[1];
        $fpath_tmp=(split /Path: /,$file[$i-1])[1];
        $fpath=(substr($fpath_tmp,0,(length($fpath_tmp)-(length($fname)+1))));
      }
      elsif( $file[$i]=~m/Conflict Previous Base File:/o )
      {
        $sbox_repo_base=(split /Conflict Previous Base File: /,$file[$i])[1];
      }
      elsif( $file[$i]=~m/Conflict Previous Working File:/o )
      {
        $sbox_repo_changed=(split /Conflict Previous Working File: /,
                            $file[$i])[1];
      }
      elsif( $file[$i]=~m/Conflict Current Base File:/o )
      {
        $sbox_repo_latest=(split /Conflict Current Base File: /,$file[$i])[1];
      }
    }

#   print "\n----------------------------------------------\n";
#   print "                      \$fpath: '$fpath'\n";
#   print "                      \$fname: '$fname'\n";
#   print "[A](mine) \$sbox_repo_changed: '$sbox_repo_changed'\n";
#   print "[B](older)   \$sbox_repo_base: '$sbox_repo_base'\n";
#   print "[C](latest)\$sbox_repo_latest: '$sbox_repo_latest'\n";
#   print "\n----------------------------------------------\n";

    # Send them in standard, ABCD, order.
    @rdat=diff_it("${fpath}/${sbox_repo_changed}",
                  "${fpath}/${sbox_repo_base}",
                  "${fpath}/${sbox_repo_latest}",
                  "${fpath}/${fname}");

    # Print out any return data.  Shouldn't be much of anything due to
    # the redirection to /dev/null in the invocation of whatever diff
    # command is used.
    print "\nOutput from diff3 command.\n";
    print "-----------------------\n";
    foreach $retline (@rdat)
    {
      print "$retline\n";
    }
    print "---------END-----------\n";

    # Add the files we may wish to remove to an array.  Keep *.mine
    # files in case something bad happened.
    push(@cleanup_array,
         "${fpath}/${sbox_repo_base}",
         "${fpath}/${sbox_repo_latest}",
         "${fpath}/${fname}.orig");

    # Make copies of *.mine for ctya purposes.
    if ( ${backup_mine_files} > 0 )
    {
      copy("${fpath}/${sbox_repo_changed}",
           "${fpath}/${fname}.${ENV{USERNAME}}");
    }

    # Return an array that contains all the files we might wish to
    # commit.
    push(@commit_array,"${fpath}/${fname}");
  }

  return \@cleanup_array, \@commit_array;

} #parse_it

sub clean_up
{
  my @args=@_;
  my @rdat;

  foreach $file (@{$args[0]})
  {
    unlink($file);
  }

  # Need to tell subversion we have resolved the conflicts.
  @rdat=@{exec_cmd("svn -R resolved . |")};
  print "\nOutput from subversion 'resolved' command.\n";
  print "-----------------------\n";
  foreach $line (@rdat)
  {
    print "$line\n";
  }
  print "---------END-----------\n";

} #clean_up

sub main
{
  my @args=@_;
  my $cleanup_aref;
  my $commit_aref;
  my $file;

  ($cleanup_aref,$commit_aref)=parse_it(svn_update_info());
  clean_up($cleanup_aref);

  print "\nDon't forget to commit these files:\n";
  print "-----------------------\n";
  foreach $file (@{$commit_aref})
  {
    print "$file\n";
  }
  print "---------END-----------\n";

} #main

# Special global.
$CHOSENDIFF="";

# Get the ball rolling.
main(@ARGV);

#!perl
##########################################################################
# FILE       mk_distro.pl
# PURPOSE    General Interface for making a Windows distribution
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

##########################################################################
# INCLUDED LIBRARY FILES
use strict;
#use File::Find;
use Cwd;
#use Win32;
require 'cmn.pl';

##########################################################################
# FUNCTION DECLARATIONS
sub Main;
sub MakeSetup;

##########################################################################
# CONSTANTS AND GLOBAL VARIABLES
my $g_AutoRun='';
my $g_MakeVersion='';
my $g_MakeDocs='';
my $g_MakeSetup='';

##########################################################################
# PROGRAM ENTRANCE

Main;

##########################################################################
# FUNCTION DEFINITIONS
#-------------------------------------------------------------------------
# FUNCTION   Main
# DOES       This is the program's main function
sub Main
{
    $g_AutoRun="y" if ($ARGV[0] eq "-a");
    my $Input='';
    
    if ($g_AutoRun)
      {
        system ("perl set_version.pl -a");
        system ("perl mk_svndoc.pl");
        &MakeSetup;
      }
    else
      {
        print "mk_distro\n\n",
          "  You can make a complete or partial part of your setup from here. Bear in mind\n",
          "  that all the files needed to make and include in the setup must be in place.\n",
          "\n  MENU:\n",
          "  -----\n",
          "  v) Set version info on files related to the setup\n",
          "  d) Make documentation of the XML files in your working Subversion repository\n",
          "  s) Make a final setup of what you have made of v and d\n",
          "  e) Make everything\n\n",
          "  q) quit\n\n",
          "  Please, select one item [v/d/s/e/q]: ";
        
        chomp ($Input = <STDIN>);
        exit if ($Input eq "q");

        $g_MakeVersion='y' if ($Input eq "v" || $Input eq "e");
        $g_MakeDocs='y' if ($Input eq "d" || $Input eq "e");
        $g_MakeSetup='y' if ($Input eq "s" || $Input eq "e");
        
        if (! $g_MakeVersion && ! $g_MakeDocs && ! $g_MakeSetup)
          {
            print "\nUh, you did not give me a v,d,s,e or q, please try again\n";
            sleep (2);
            &Main;
          }

        system ("perl set_version.pl") if ($g_MakeVersion);
        system ("perl mk_svndoc.pl")if ($g_MakeDocs);
        &MakeSetup if ($g_MakeSetup);
      }
}

#-------------------------------------------------------------------------
# FUNCTION   MakeSetup
# DOES       Making the Setup file
sub MakeSetup
{
    my $SetupOut=&PathSetupOut;
    my $PathISExe=&PathISExe;
    my $RetVal=0;

    chdir '..';

    if (! $g_AutoRun)
      {
        print "Compiling the setup (take a nap, this will take some time)...\n";;
      }

    $RetVal=`$PathISExe svn.iss`;
}

#-------------------------------------------------------------------------------
# FUNCTION PathISExe
# DOES     Finding and returning the current svn.exe path as of
#          ..\svn_iss_dyn.iss
sub PathISExe
{
    my $PathISExe = &cmn_ValuePathfile('path_is');
  
    if ( ! -e "$PathISExe/ISCC.exe")
      {
        die "ERROR: Could not find path to ISCC.exe in svn_dynamics.iss\n";
      }
    
    $PathISExe = "$PathISExe\\ISCC.exe";
    return $PathISExe;
}

#-------------------------------------------------------------------------------
# FUNCTION PathSetupOut
# DOES     Finding and returning the current svn.exe path as of
#          ..\svn_iss_dyn.iss
sub PathSetupOut
{
    my $SetupOut = &cmn_ValuePathfile('path_setup_out');
  
    if ( ! -e "../$SetupOut")
      {
        die "ERROR: Could not find output directory as described in svn_dynamics.iss\n";
      }

    return $SetupOut;
}

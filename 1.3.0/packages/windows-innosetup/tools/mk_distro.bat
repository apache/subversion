@rem = '--*-Perl-*--
@echo off
if "%OS%" == "Windows_NT" goto WinNT
perl -x -S "%0" %1 %2 %3 %4 %5 %6 %7 %8 %9
goto endofperl
:WinNT
perl -x -S %0 %*
if NOT "%COMSPEC%" == "%SystemRoot%\system32\cmd.exe" goto endofperl
if %errorlevel% == 9009 echo You do not have Perl in your PATH.
if errorlevel 1 goto script_failed_so_exit_with_non_zero_val 2>nul
goto endofperl
@rem ';
#!perl
#line 15
##########################################################################
# FILE       mk_distro.bat
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
use Cwd;
use File::Basename;

##########################################################################
# FUNCTION DECLARATIONS
sub Main;

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
    my $CurOrig=getcwd;
    my $DirName=dirname($0);

    if ($DirName eq '.')
      {
        system ("perl mk_distro.pl @ARGV");
      }
    else
      {
        chdir $DirName;
        system ("perl mk_distro.pl @ARGV");
        chdir $CurOrig;
      }
}

__END__
:endofperl

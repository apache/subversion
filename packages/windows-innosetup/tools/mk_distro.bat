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
#    Licensed to the Subversion Corporation (SVN Corp.) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The SVN Corp. licenses this file
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

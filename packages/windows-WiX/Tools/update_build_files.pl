#!perl
################################################################################
# FILE     update_build_files.pl
# PURPOSE  Setting version info on build files for WiX
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

################################################################################
# INCLUDED LIBRARY FILES
use strict;
use Cwd;
use Win32;
use Win32::Guidgen;
require 'cmn.pl';
use File::Basename;

################################################################################
# FUNCTION DECLARATIONS
sub Main;

################################################################################
# CONSTANTS AND GLOBAL VARIABLES
my $g_AutoRun='';

##########################################################################
# PROGRAM ENTRANCE
Main;

################################################################################
# FUNCTION DEFINITIONS
#-------------------------------------------------------------------------------
# FUNCTION   Main
# DOES       This is the program's main function
sub Main
{
	my $SvnVersion=&cmn_ValuePathfile('svn_version.ini', 'svn_version');
	my $SvnRevision=&cmn_ValuePathfile('svn_version.ini', 'svn_revision');
	my $guid = Win32::Guidgen::create();

	&UpdateXMLFile('..\\BuildSubversion\\BuildSubversion.wixproj', '/Project/PropertyGroup/OutputName', 'Setup-Subversion-'.$SvnVersion);
	&UpdateXMLFile('..\\BuildSubversion\\Setup.wxs', '//processing-instruction("define")[2]', 'ProductVersion="'.$SvnVersion.'"', 'y');
	&UpdateXMLFile('..\\BuildSubversion\\Setup.wxs', '//processing-instruction("define")[3]', 'RevisionNumber="r'.$SvnRevision.'"', 'y');
	&UpdateXMLFile('..\\BuildSubversion\\Setup.wxs', '//processing-instruction("define")[4]', 'ProductCode="'.$guid.'"', 'y');
}

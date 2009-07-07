#!perl
################################################################################
# FILE     update_build_files.pl
# PURPOSE  Setting version info on build files for WiX
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

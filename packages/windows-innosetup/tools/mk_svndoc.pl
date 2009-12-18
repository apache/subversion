#!perl
##########################################################################
# FILE       mk_svndoc
# PURPOSE    Making MS HTML-help from the Subversion source documentation
# ====================================================================
# Copyright (c) 2000-2005 CollabNet.  All rights reserved.
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
use File::Basename;
use Cwd;
use Win32;
require 'cmn.pl';

##########################################################################
# FUNCTION DECLARATIONS
sub Main;
sub CheckForProgs;
sub CopyAndEolU2W;
sub MkDirP;

##########################################################################
# CONSTANTS AND GLOBAL VARIABLES
# Sorces and destinations
my $g_PathDocRoot=&cmn_ValuePathfile('path_svnbook');
my $g_PathSubvRoot=&cmn_ValuePathfile('path_subversion');
my $g_PathMiscIn=&cmn_ValuePathfile('path_setup_in');
my %g_FilesToCpAndConv=
    (
        'COPYING', 'subversion\SubversionLicense.txt',
        'README', 'subversion\Readme.dist',
        'doc\user\lj_article.txt', 'doc\lj_article.txt',
        'doc\programmer\WritingChangeLogs.txt', 'doc\WritingChangeLogs.txt',
    );

# Programs needed for making the documentation
my $g_Prog_hhc='';
my %g_ProgsInPath=
    (
        'libxslt is needed for converting the XML-documentaion.',
            'xsltproc.exe',
        'iconv is needed by libxslt and libxslt for converting the XML-documentaion.',
            'iconv.exe',
        'libxml2 is needed by libxslt for converting the XML-documentaion.',
            'libxml2.dll'
    );

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
    my $CntMkHtmBat='';
    my %Values;
    my $RootSvnBook=&cmn_ValuePathfile('path_svnbook');
    my $DocOut=&cmn_ValuePathfile('path_setup_in');
    my $PathWinIsPack='';
    my $Pwd='';
    my $CntVerXml='';

    # Get absolute path of the current PWD's parent
    $PathWinIsPack=getcwd;
    $Pwd=basename($PathWinIsPack);
    $PathWinIsPack =~ s/\//\\/g;
    $PathWinIsPack =~ s/\\$Pwd$//;

    # Make $DocOut to an absolute path
    if ($DocOut eq 'in')
      {
        $DocOut = "$PathWinIsPack\\in\\doc";
      }

    #Make the out dir in "$RootSvnBook\src if needed
    &MkDirP ("$RootSvnBook\\src\\out") unless (-e "$RootSvnBook\\src\\out");

    #Make the in dir for path_setup_in if needed
    &MkDirP ("$DocOut") unless (-e "$DocOut");

    #Check for needed programs
    &CheckForProgs;
    &CopyAndEolU2W;

    #Create the mk_htmlhelp.bat file from ..\templates\\mk_htmlhelp.bat and
    #collected data, save it to $RootSvnBook\src\out\mk_htmlhelp.bat
    %Values =
      (
        tv_path_hhc => $g_Prog_hhc,
        tv_bookdest => $DocOut
      );

    $CntMkHtmBat=&cmn_Template("$PathWinIsPack\\templates\\mk_htmlhelp.bat", \%Values);

    open (FH_HHP, ">" . "$RootSvnBook\\src\\out\\mk_htmlhelp.bat");
        print FH_HHP $CntMkHtmBat;
    close (FH_HHP);

    #Copy style sheet and background image to $RootSvnBook\src\out
    system ("copy /Y ..\\templates\\svn-doc.css $RootSvnBook\\src\\out");
    system ("copy /Y ..\\images\\svn_bck.png $RootSvnBook\\src\\out");

    # Set the revision number in $RootSvnBook\src\en\book\version.xml
    chdir "$RootSvnBook\\src\\en";
    $CntVerXml=`svnversion .`;
    chomp($CntVerXml);

    open (FH_VERXML, ">" . "$RootSvnBook\\src\\en\\book\\version.xml");
        print FH_VERXML "<!ENTITY svn.version \"Revision $CntVerXml\">";
    close (FH_VERXML);

    # Make the chm file
    chdir "$RootSvnBook\\src\\out";
    system ("$RootSvnBook\\src\\out\\mk_htmlhelp.bat");
    chdir $Pwd;
}

#-------------------------------------------------------------------------
# FUNCTION   CheckForProgs
# DOES       Checking if required programs exists
sub CheckForProgs
{
    my @MissingProgs;
    my @SysPath;
    my $bMissingProgs=0;
    my $Prog2CheckDesc='';
    my $Prog2Check='';

    # Fill the %PATH% in @SysPath
    @SysPath = split (/;/, $ENV{PATH});

    # Check for the needed programs who should be in the %PATH%
    while (($Prog2CheckDesc, $Prog2Check) = each %g_ProgsInPath)
      {
        my $bProg2CheckExists=0;

        for (@SysPath)
          {
            if (-e "$_\\$Prog2Check")
              {
                $bProg2CheckExists=1;
                last;
              }
          }

        if (! $bProg2CheckExists)
          {
            $bMissingProgs = 1;
            push @MissingProgs, $Prog2CheckDesc;
          }
      }

    # Check for MS HTML help compiler
    $g_Prog_hhc = &cmn_ValuePathfile('path_hhc');
    $g_Prog_hhc = "$g_Prog_hhc\\hhc.exe";

    if (! -e $g_Prog_hhc)
      {
        $bMissingProgs = 1;
        push @MissingProgs, 'Microsoft HTML Help Workshop is needed for making the HTML-help file';
      }
    $g_Prog_hhc = "\"$g_Prog_hhc\"";

    if ($bMissingProgs)
      {
        my $Msg="One or more required programs needed for making the docs are missing:\n\n";

        for (@MissingProgs)
          {
            $Msg=$Msg . "  - $_\n";
          }

          $Msg=$Msg . "\nPlease, check that everything are installed properly as described in\n";
          $Msg=$Msg . "the documentation in packages\\windows-innosetup\\tools\\readme.txt\n";
          Win32::MsgBox($Msg, 0+MB_ICONSTOP, 'ERROR: Missing required programs.');
          exit 1;
      }
}

#-------------------------------------------------------------------------
# FUNCTION   CopyAndEolU2W
# DOES       Converts Unix eol's to Windows eol's in a file and saves it to
#            another location.
sub CopyAndEolU2W
{
    my $FileSrc='';
    my $FileDest='';
    my $FileCnt='';
    my $PathWinIsPack='';
    my $Pwd='';

    # Get absolute path of the current PWD's parent
    $PathWinIsPack=getcwd;
    $Pwd=basename($PathWinIsPack);
    $PathWinIsPack =~ s/\//\\/g;
    $PathWinIsPack =~ s/\\$Pwd$//;

    while (($FileSrc, $FileDest) = each %g_FilesToCpAndConv)
      {
#        $FileSrc = "$g_PathDocRoot\\$FileSrc";
        $FileSrc = "$g_PathSubvRoot\\$FileSrc";
        $FileDest = "$PathWinIsPack\\$g_PathMiscIn\\$FileDest";
        print "Copying and converting EOL's from $FileSrc to $FileDest\n";

        open (FH_SRC, $FileSrc);
            while (<FH_SRC>)
              {
                chomp($_);
                $_ = "$_\r\n";

                if ($FileCnt)
                  {
                    $FileCnt = $FileCnt . $_;
                  }
                else
                  {
                    $FileCnt = $_;
                  }
              }
         close (FH_SRC);

        #Make the in dir for path_setup_in if needed
        &MkDirP (dirname($FileDest)) unless (-e dirname($FileDest));

        open (FH_DEST, ">" . $FileDest);
            print FH_DEST $FileCnt;
        close (FH_DEST);

        $FileCnt='';
      }
}
